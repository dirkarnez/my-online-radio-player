#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <curl/curl.h>
#include <portaudio.h>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 2
#define SAMPLE_FORMAT paInt16

std::atomic<bool> keepRunning(true);

struct AudioBuffer {
    std::vector<char> data;
    std::mutex mutex;
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    AudioBuffer* buffer = static_cast<AudioBuffer*>(userp);
    std::lock_guard<std::mutex> lock(buffer->mutex);
    buffer->data.insert(buffer->data.end(), (char*)contents, (char*)contents + totalSize);
    return totalSize;
}

void audioThread(AudioBuffer* buffer) {
    PaStream* stream;
    Pa_Initialize();
    Pa_OpenDefaultStream(&stream, 0, NUM_CHANNELS, SAMPLE_FORMAT, SAMPLE_RATE, FRAMES_PER_BUFFER, nullptr, nullptr);
    Pa_StartStream(stream);

    while (keepRunning) {
        std::vector<char> chunk;
        {
            std::lock_guard<std::mutex> lock(buffer->mutex);
            if (buffer->data.size() >= FRAMES_PER_BUFFER * NUM_CHANNELS * 2) { // 2 bytes/sample for int16
                chunk.assign(buffer->data.begin(), buffer->data.begin() + FRAMES_PER_BUFFER * NUM_CHANNELS * 2);
                buffer->data.erase(buffer->data.begin(), buffer->data.begin() + FRAMES_PER_BUFFER * NUM_CHANNELS * 2);
            }
        }
        if (!chunk.empty()) {
            Pa_WriteStream(stream, chunk.data(), FRAMES_PER_BUFFER);
        } else {
            Pa_Sleep(10);
        }
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <Icecast stream URL>" << std::endl;
        return 1;
    }

    AudioBuffer buffer;

    std::thread audio(audioThread, &buffer);

    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, argv[1]);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    keepRunning = false;
    audio.join();

    curl_easy_cleanup(curl);

    return 0;
}
