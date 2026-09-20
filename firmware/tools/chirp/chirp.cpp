// Renders chirps from src/chirp.h to WAV files so they can be auditioned on a
// laptop before flashing. Runs the very same Chirp::play() the firmware runs,
// with an output policy that appends samples instead of toggling a pin, so a
// file here is what the buzzer pin gets fed — a raw square wave. The piezo
// itself colours it (loudest around 2-5kHz); --piezo applies a rough
// band-pass to hint at that, and is a hint only.
//
//   ./chirp --happiness 200 --count 8         # out/chirp-h200-s1.wav ...
//   ./chirp --bands                           # out/band-{sad,glum,content,happy}.wav
//   ./chirp --ladder                          # out/ladder.wav, 255 -> 0 in one file
//
// Every chirp is also printed as the same one-line description the firmware
// logs on Serial, so a file can be matched to a log line later.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include <string>

#include "../../src/chirp.h"

struct WavOut {
    uint32_t rate;
    bool level = false;
    uint64_t acc = 0;  // Fractional microseconds carried across wait() calls
    std::vector<int16_t> samples;

    explicit WavOut(uint32_t r) : rate(r) {}
    void set(bool high) { level = high; }
    void wait(uint32_t us) {
        acc += (uint64_t)us * rate;
        while (acc >= 1000000ULL) {
            samples.push_back(level ? 16384 : -16384);
            acc -= 1000000ULL;
        }
    }
    void silence(uint32_t ms) {
        level = false;
        wait(ms * 1000);
    }
};

// Second-order band-pass, ~3.6kHz, Q 1.2: a caricature of a small piezo disc.
static void piezoFilter(std::vector<int16_t>& s, uint32_t rate) {
    const double f0 = 3600.0, q = 1.2;
    const double w0 = 2 * M_PI * f0 / rate, alpha = sin(w0) / (2 * q);
    const double b0 = alpha, b1 = 0, b2 = -alpha;
    const double a0 = 1 + alpha, a1 = -2 * cos(w0), a2 = 1 - alpha;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < s.size(); i++) {
        double x = s[i];
        double y = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        double v = y * 2.5;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        s[i] = (int16_t)v;
    }
}

static void put32(FILE* f, uint32_t v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); fputc((v >> 16) & 255, f); fputc(v >> 24, f); }
static void put16(FILE* f, uint16_t v) { fputc(v & 255, f); fputc(v >> 8, f); }

static bool writeWav(const std::string& path, const std::vector<int16_t>& s, uint32_t rate) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return false; }
    const uint32_t dataBytes = (uint32_t)s.size() * 2;
    fwrite("RIFF", 1, 4, f); put32(f, 36 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, dataBytes);
    for (int16_t v : s) put16(f, (uint16_t)v);
    fclose(f);
    return true;
}

static void describeLine(const Chirp::Score& s, const char* tag) {
    char desc[64];
    Chirp::describe(s, desc, sizeof(desc));
    printf("%-28s happiness=%3u band=%u %-40s = %3lums\n", tag, s.happiness, Chirp::band(s.happiness), desc,
           (unsigned long)Chirp::totalMs(s));
}

static void usage() {
    fprintf(stderr,
            "usage: chirp [--happiness 0-255] [--count N] [--seed S] [--rate HZ] [--out DIR] [--piezo]\n"
            "             [--bands] [--ladder]\n");
}

int main(int argc, char** argv) {
    int happiness = 200, count = 4;
    uint32_t seed = 1, rate = 48000;
    std::string outDir = "out";
    bool piezo = false, bands = false, ladder = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](int& i) -> const char* { if (i + 1 >= argc) { usage(); exit(2); } return argv[++i]; };
        if (a == "--happiness") happiness = atoi(val(i));
        else if (a == "--count") count = atoi(val(i));
        else if (a == "--seed") seed = (uint32_t)strtoul(val(i), nullptr, 10);
        else if (a == "--rate") rate = (uint32_t)strtoul(val(i), nullptr, 10);
        else if (a == "--out") outDir = val(i);
        else if (a == "--piezo") piezo = true;
        else if (a == "--bands") bands = true;
        else if (a == "--ladder") ladder = true;
        else { usage(); return 2; }
    }
    if (happiness < 0) happiness = 0;
    if (happiness > 255) happiness = 255;
    mkdir(outDir.c_str(), 0755);

    auto finish = [&](WavOut& w, const std::string& name) {
        if (piezo) piezoFilter(w.samples, rate);
        std::string path = outDir + "/" + name;
        if (writeWav(path, w.samples, rate)) printf("wrote %s (%.2fs)\n", path.c_str(), (double)w.samples.size() / rate);
    };

    if (bands) {
        const char* names[4] = {"sad", "glum", "content", "happy"};
        const uint8_t centre[4] = {24, 96, 160, 232};
        for (int b = 0; b < 4; b++) {
            WavOut w(rate);
            w.silence(200);
            for (int k = 0; k < 8; k++) {
                Chirp::Rng r(seed + (uint32_t)(b * 100 + k));
                Chirp::Score s = Chirp::compose(centre[b], r);
                char tag[40];
                snprintf(tag, sizeof(tag), "band-%s.wav #%d", names[b], k + 1);
                describeLine(s, tag);
                Chirp::play(s, w);
                w.silence(500);
            }
            finish(w, std::string("band-") + names[b] + ".wav");
        }
    }

    if (ladder) {
        WavOut w(rate);
        w.silence(200);
        int k = 0;
        for (int h = 255; h >= 0; h -= 32, k++) {
            Chirp::Rng r(seed + (uint32_t)k * 7919);
            Chirp::Score s = Chirp::compose((uint8_t)h, r);
            char tag[40];
            snprintf(tag, sizeof(tag), "ladder.wav h=%d", h);
            describeLine(s, tag);
            Chirp::play(s, w);
            w.silence(700);
        }
        finish(w, "ladder.wav");
    }

    if (!bands && !ladder) {
        for (int k = 0; k < count; k++) {
            uint32_t sd = seed + (uint32_t)k;
            Chirp::Rng r(sd);
            Chirp::Score s = Chirp::compose((uint8_t)happiness, r);
            char name[48];
            snprintf(name, sizeof(name), "chirp-h%d-s%u.wav", happiness, sd);
            describeLine(s, name);
            WavOut w(rate);
            w.silence(50);
            Chirp::play(s, w);
            w.silence(100);
            finish(w, name);
        }
    }
    return 0;
}
