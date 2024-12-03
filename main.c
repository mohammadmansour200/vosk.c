#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vosk_api.h>

#ifdef _WIN32
    #include <windows.h>
    // Windows-specific synchronization
    typedef HANDLE mutex_t;
    typedef HANDLE cond_t;
    #define mutex_init(m) (*(m) = CreateMutex(NULL, FALSE, NULL))
    #define mutex_lock(m) WaitForSingleObject(*(m), INFINITE)
    #define mutex_unlock(m) ReleaseMutex(*(m))
    #define cond_init(c) (*(c) = CreateEvent(NULL, FALSE, FALSE, NULL))
    #define cond_signal(c) SetEvent(*(c))
    #define cond_wait(c, m) (ReleaseMutex(*(m)), WaitForSingleObject(*(c), INFINITE), WaitForSingleObject(*(m), INFINITE))
#else
    #include <pthread.h>
    // POSIX typedefs for compatibility
    typedef pthread_mutex_t mutex_t;
    typedef pthread_cond_t cond_t;
    #define mutex_init(m) pthread_mutex_init(m, NULL)
    #define mutex_lock(m) pthread_mutex_lock(m)
    #define mutex_unlock(m) pthread_mutex_unlock(m)
    #define cond_init(c) pthread_cond_init(c, NULL)
    #define cond_signal(c) pthread_cond_signal(c)
    #define cond_wait(c, m) pthread_cond_wait(c, m)
#endif

#define CHUNK_SIZE 4000
#define QUEUE_SIZE 100

// Circular buffer for audio chunks
typedef struct {
    char *buffers[QUEUE_SIZE];
    size_t sizes[QUEUE_SIZE];
    int read_pos;
    int write_pos;
    int count;
    mutex_t lock;
    cond_t not_empty;
    cond_t not_full;
} AudioQueue;

typedef struct {
    FILE *wavin;
    VoskRecognizer *recognizer;
    AudioQueue *queue;
    long total_bytes_read;
    long file_size;
    int is_done;
} SharedData;

// Initialize queue
AudioQueue* queue_init() {
    AudioQueue *q = malloc(sizeof(AudioQueue));
    q->read_pos = 0;
    q->write_pos = 0;
    q->count = 0;
    mutex_init(&q->lock);
    cond_init(&q->not_empty);
    cond_init(&q->not_full);
    
    for (int i = 0; i < QUEUE_SIZE; i++) {
        q->buffers[i] = malloc(CHUNK_SIZE);
        q->sizes[i] = 0;
    }
    return q;
}

#ifdef _WIN32
DWORD WINAPI read_audio(LPVOID arg) {
#else
void *read_audio(void *arg) {
#endif
    SharedData *shared_data = (SharedData *)arg;
    AudioQueue *q = shared_data->queue;
    FILE *wavin = shared_data->wavin;

    while (1) {
        mutex_lock(&q->lock);
        while (q->count >= QUEUE_SIZE) {
            cond_wait(&q->not_full, &q->lock);
        }

        size_t nread = fread(q->buffers[q->write_pos], 1, CHUNK_SIZE, wavin);
        q->sizes[q->write_pos] = nread;
        shared_data->total_bytes_read += nread;

        float progress = (float)shared_data->total_bytes_read / shared_data->file_size * 100.0f;
        fprintf(stderr, "\rProgress: %.2f%%", progress);
        fflush(stderr);

        if (nread > 0) {
            q->write_pos = (q->write_pos + 1) % QUEUE_SIZE;
            q->count++;
            cond_signal(&q->not_empty);
        }

        if (nread < CHUNK_SIZE) {
            shared_data->is_done = 1;
            mutex_unlock(&q->lock);
            break;
        }
        mutex_unlock(&q->lock);
    }
    return 0;
}

#ifdef _WIN32
DWORD WINAPI process_audio(LPVOID arg) {
#else
void *process_audio(void *arg) {
#endif
    SharedData *shared_data = (SharedData *)arg;
    AudioQueue *q = shared_data->queue;
    VoskRecognizer *recognizer = shared_data->recognizer;

    while (!shared_data->is_done || q->count > 0) {
        mutex_lock(&q->lock);
        while (q->count == 0 && !shared_data->is_done) {
            cond_wait(&q->not_empty, &q->lock);
        }

        if (q->count > 0) {
            size_t size = q->sizes[q->read_pos];
            vosk_recognizer_accept_waveform(recognizer, q->buffers[q->read_pos], size);

            q->read_pos = (q->read_pos + 1) % QUEUE_SIZE;
            q->count--;
            cond_signal(&q->not_full);
        }
        mutex_unlock(&q->lock);
    }

    const char *final_result = vosk_recognizer_final_result(recognizer);
    printf("\nFinal Result: %s\n", final_result);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <model_path> <wav_file>\n", argv[0]);
        return 1;
    }

    VoskModel *model = vosk_model_new(argv[1]);
    VoskRecognizer *recognizer = vosk_recognizer_new(model, 16000.0);
    vosk_recognizer_set_words(recognizer, 1);

    FILE *wavin = fopen(argv[2], "rb");
    if (!wavin) {
        fprintf(stderr, "Error opening file: %s\n", argv[2]);
        return 1;
    }

    fseek(wavin, 44, SEEK_SET);
    fseek(wavin, 0, SEEK_END);
    long file_size = ftell(wavin);
    fseek(wavin, 44, SEEK_SET);

    AudioQueue *queue = queue_init();
    SharedData shared_data = {
        .wavin = wavin,
        .recognizer = recognizer,
        .queue = queue,
        .total_bytes_read = 0,
        .file_size = file_size - 44,
        .is_done = 0
    };

#ifdef _WIN32
    HANDLE read_thread = CreateThread(NULL, 0, read_audio, &shared_data, 0, NULL);
    HANDLE process_thread = CreateThread(NULL, 0, process_audio, &shared_data, 0, NULL);
    if (read_thread == NULL || process_thread == NULL) {
        fprintf(stderr, "Error creating threads\n");
        return 1;
    }
    WaitForSingleObject(read_thread, INFINITE);
    WaitForSingleObject(process_thread, INFINITE);
    CloseHandle(read_thread);
    CloseHandle(process_thread);
#else
    pthread_t read_thread, process_thread;
    pthread_create(&read_thread, NULL, read_audio, &shared_data);
    pthread_create(&process_thread, NULL, process_audio, &shared_data);
    pthread_join(read_thread, NULL);
    pthread_join(process_thread, NULL);
#endif

    for (int i = 0; i < QUEUE_SIZE; i++) {
        free(queue->buffers[i]);
    }
    free(queue);
    vosk_recognizer_free(recognizer);
    vosk_model_free(model);
    fclose(wavin);

    return 0;
}