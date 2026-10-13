#include "meeting_service.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "cJSON.h"
#include "chroninkle_task_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gemini_service.h"
#include "recording_archive_service.h"
#include "recording_service.h"
#include "sdkconfig.h"
#include "storage_service.h"

namespace meeting_service {
namespace {

constexpr const char* kTag = "meeting_service";
constexpr const char* kMeetingsDir = "meetings";
// Segments that kept failing are parked here (not deleted) so no audio is ever lost.
constexpr const char* kFailedDir = "failed";
constexpr const char* kTranscriptFile = "transcript.txt";
constexpr const char* kNoteFile = "note.txt";
constexpr const char* kMetaFile = "meeting.json";

constexpr uint32_t kSegmentSeconds = CONFIG_CHRONINKLE_MEETING_SEGMENT_SECONDS;
// The writer drains the capture ring this often but only touches the SD card every
// kFlushSeconds, so the card (and the storage busy flag) is hit a few times a minute.
constexpr uint32_t kDrainIntervalMs = 500;
constexpr uint32_t kFlushSeconds = 4;
constexpr uint32_t kAccumSeconds = 6;
constexpr int kMaxWriteFailures = 3;
constexpr uint64_t kMinFreeBytes = 16ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMinSegmentMs = 500;
constexpr uint32_t kRetryBaseMs = 30000;
constexpr uint32_t kRetryMaxMs = 5U * 60U * 1000U;
constexpr int kMaxFailuresBeforePark = 6;
constexpr uint32_t kWaitingRecheckMs = 60000;
constexpr uint32_t kStorageRecheckMs = 60000;
constexpr uint32_t kBootScanDelayMs = 8000;
constexpr uint32_t kWaitForever = UINT32_MAX;
constexpr size_t kTailChars = 600;
constexpr uint32_t kWriterStackWords = 4096;
constexpr uint32_t kTranscriberStackWords = 6144;
constexpr size_t kLoadChunkSamples = 4096;
constexpr size_t kWavHeaderBytes = 44;
constexpr uint32_t kShutdownFlushWaitMs = 3000;

std::mutex s_mutex;
bool s_initialized = false;
TaskHandle_t s_writer_task = nullptr;
TaskHandle_t s_transcriber_task = nullptr;
EventHandler s_event_handler = nullptr;
void* s_event_context = nullptr;

// Current (or most recent) meeting.
bool s_active = false;
bool s_stop_requested = false;
// The writer still owns the meeting's open segment (capturing, or flushing the tail).
bool s_writer_busy = false;
std::string s_meeting_id;
std::string s_writer_dir;  // non-empty while the writer owns a segment in this meeting
int s_current_segment = 0;
uint64_t s_samples_total = 0;
uint32_t s_sample_rate = 16000;
int64_t s_created_unix = 0;
int s_segments_recorded = 0;
int s_segments_transcribed = 0;

// Transcriber state.
int s_segments_pending = 0;
bool s_transcribing = false;
bool s_waiting_for_network = false;
bool s_retry_scheduled = false;
std::string s_last_error;
std::string s_tail;
std::string s_saved_id;

// Writer-owned (only touched by the writer task while s_writer_busy, or by Start() before
// it sets s_writer_busy).
recording_service::PsramVector<int16_t> s_accum;
size_t s_accum_len = 0;
size_t s_segment_written = 0;
int s_write_failures = 0;

// Transcriber-owned failure tracking for the segment at the head of the queue.
std::string s_fail_key;
int s_fail_count = 0;

std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) {
        return b;
    }
    if (a.back() == '/') {
        return a + b;
    }
    return a + "/" + b;
}

std::string MeetingsRoot(const char* mount_point)
{
    return JoinPath(mount_point, kMeetingsDir);
}

std::string MeetingDir(const char* mount_point, const std::string& id)
{
    return JoinPath(MeetingsRoot(mount_point), id);
}

std::string SegmentName(int index)
{
    char name[24] = {};
    std::snprintf(name, sizeof(name), "seg_%04d.wav", index);
    return name;
}

bool ParseSegmentName(const char* name, int* index)
{
    int value = 0;
    char tail[8] = {};
    if (std::sscanf(name, "seg_%d.%7s", &value, tail) != 2 || std::strcmp(tail, "wav") != 0 ||
        value <= 0) {
        return false;
    }
    *index = value;
    return true;
}

bool EnsureDir(const std::string& path)
{
    if (mkdir(path.c_str(), 0775) == 0 || errno == EEXIST) {
        return true;
    }
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

long FileSize(const std::string& path)
{
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long>(st.st_size);
}

bool WriteWholeFile(const std::string& path, const std::string& data, const char* mode = "wb")
{
    FILE* file = std::fopen(path.c_str(), mode);
    if (file == nullptr) {
        return false;
    }
    const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
    return (std::fclose(file) == 0) && wrote;
}

bool ReadWholeFile(const std::string& path, std::string* out)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    out->clear();
    char buffer[512];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out->append(buffer, read);
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    return ok;
}

void PutLe16(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void PutLe32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
}

void BuildWavHeader(uint8_t* out, uint32_t sample_rate, uint32_t data_bytes)
{
    std::memcpy(out, "RIFF", 4);
    PutLe32(out + 4, 36U + data_bytes);
    std::memcpy(out + 8, "WAVEfmt ", 8);
    PutLe32(out + 16, 16);
    PutLe16(out + 20, 1);
    PutLe16(out + 22, 1);
    PutLe32(out + 24, sample_rate);
    PutLe32(out + 28, sample_rate * 2U);
    PutLe16(out + 32, 2);
    PutLe16(out + 34, 16);
    std::memcpy(out + 36, "data", 4);
    PutLe32(out + 40, data_bytes);
}

std::string BuildMetaJson(int64_t created_unix, uint32_t duration_ms, bool stopped)
{
    char json[128] = {};
    std::snprintf(json, sizeof(json),
                  "{\"version\":1,\"created_unix\":%lld,\"duration_ms\":%lu,\"stopped\":%s}",
                  static_cast<long long>(created_unix), static_cast<unsigned long>(duration_ms),
                  stopped ? "true" : "false");
    return json;
}

void ParseMetaJson(const std::string& json, int64_t* created_unix, uint32_t* duration_ms)
{
    cJSON* root = cJSON_ParseWithLength(json.c_str(), json.size());
    if (root == nullptr) {
        return;
    }
    const cJSON* created = cJSON_GetObjectItemCaseSensitive(root, "created_unix");
    if (cJSON_IsNumber(created)) {
        *created_unix = static_cast<int64_t>(created->valuedouble);
    }
    const cJSON* duration = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    if (cJSON_IsNumber(duration) && duration->valuedouble >= 0) {
        *duration_ms = static_cast<uint32_t>(duration->valuedouble);
    }
    cJSON_Delete(root);
}

uint32_t SamplesToMs(uint64_t samples, uint32_t rate)
{
    return rate == 0 ? 0 : static_cast<uint32_t>((samples * 1000ULL) / rate);
}

Snapshot BuildSnapshotLocked()
{
    Snapshot snapshot = {};
    snapshot.active = s_active;
    snapshot.elapsed_ms = SamplesToMs(s_samples_total, s_sample_rate);
    snapshot.segments_recorded = s_segments_recorded;
    snapshot.segments_transcribed = s_segments_transcribed;
    snapshot.segments_pending = s_segments_pending;
    snapshot.transcribing = s_transcribing;
    snapshot.waiting_for_network = s_waiting_for_network;
    snapshot.retry_scheduled = s_retry_scheduled;
    snapshot.last_error = s_last_error;
    snapshot.transcript_tail = s_tail;
    snapshot.saved_recording_id = s_saved_id;
    return snapshot;
}

void Emit(EventType type)
{
    EventHandler handler = nullptr;
    void* context = nullptr;
    Event event = {};
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        handler = s_event_handler;
        context = s_event_context;
        event.type = type;
        event.snapshot = BuildSnapshotLocked();
    }
    if (handler != nullptr) {
        handler(event, context);
    }
}

bool GeminiReady()
{
    return gemini_service::GetSnapshot().runtime.ready;
}

bool StorageUsable()
{
    return storage_service::GetSnapshot().inserted && !storage_service::IsUsbModeActive();
}

// ---------------------------------------------------------------------------------------
// SD handlers (run under storage_service's card lock)
// ---------------------------------------------------------------------------------------

struct StartContext {
    const std::string* id = nullptr;
    std::string meta;
};

esp_err_t StartOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* start = static_cast<StartContext*>(context);
    if (!EnsureDir(MeetingsRoot(mount_point))) {
        return ESP_FAIL;
    }
    const std::string dir = MeetingDir(mount_point, *start->id);
    if (!EnsureDir(dir)) {
        return ESP_FAIL;
    }
    if (!WriteWholeFile(JoinPath(dir, kTranscriptFile), {}) ||
        !WriteWholeFile(JoinPath(dir, kMetaFile), start->meta)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

struct AppendAudioContext {
    std::string id;
    int segment = 0;
    const int16_t* samples = nullptr;
    size_t sample_count = 0;
    bool close_segment = false;
    uint32_t sample_rate = 16000;
    // Optional meta.json rewrite (at segment close / meeting end).
    std::string meta;
};

esp_err_t AppendAudioOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* append = static_cast<AppendAudioContext*>(context);
    const std::string dir = MeetingDir(mount_point, append->id);
    const std::string path = JoinPath(dir, SegmentName(append->segment));

    if (append->sample_count > 0) {
        FILE* file = std::fopen(path.c_str(), "ab");
        if (file == nullptr) {
            ESP_LOGW(kTag, "Open %s for append failed: errno=%d", path.c_str(), errno);
            return ESP_FAIL;
        }
        bool ok = true;
        std::fseek(file, 0, SEEK_END);
        if (std::ftell(file) == 0) {
            // Placeholder header; sizes are patched when the segment closes. A segment
            // orphaned by a power cut is still readable -- the loader trusts the file size.
            uint8_t header[kWavHeaderBytes] = {};
            BuildWavHeader(header, append->sample_rate, 0);
            ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
        }
        const size_t bytes = append->sample_count * sizeof(int16_t);
        ok = ok && std::fwrite(append->samples, 1, bytes, file) == bytes;
        ok = (std::fclose(file) == 0) && ok;
        if (!ok) {
            ESP_LOGW(kTag, "Append to %s failed", path.c_str());
            return ESP_FAIL;
        }
    }

    if (append->close_segment) {
        const long size = FileSize(path);
        if (size >= static_cast<long>(kWavHeaderBytes)) {
            FILE* file = std::fopen(path.c_str(), "r+b");
            if (file != nullptr) {
                uint8_t header[kWavHeaderBytes] = {};
                BuildWavHeader(header, append->sample_rate,
                               static_cast<uint32_t>(size - static_cast<long>(kWavHeaderBytes)));
                (void)std::fwrite(header, 1, sizeof(header), file);
                std::fclose(file);
            }
        }
    }

    if (!append->meta.empty()) {
        (void)WriteWholeFile(JoinPath(dir, kMetaFile), append->meta);
    }
    return ESP_OK;
}

struct PendingMeeting {
    std::string id;
    std::vector<int> closed_segments;
    bool writer_owned = false;
    int64_t created_unix = 0;
    uint32_t duration_ms = 0;
};

struct ScanContext {
    std::string writer_dir;
    int writer_segment = 0;
    std::vector<PendingMeeting> meetings;
};

esp_err_t ScanOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* scan = static_cast<ScanContext*>(context);
    const std::string root = MeetingsRoot(mount_point);
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) {
        return ESP_OK;  // no meetings yet
    }
    std::vector<std::string> ids;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.' ||
            std::strcmp(entry->d_name, kFailedDir) == 0) {
            continue;
        }
        ids.emplace_back(entry->d_name);
    }
    closedir(dir);
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        PendingMeeting meeting = {};
        meeting.id = id;
        meeting.writer_owned = (id == scan->writer_dir);
        const std::string meeting_dir = JoinPath(root, id);
        DIR* inner = opendir(meeting_dir.c_str());
        if (inner == nullptr) {
            continue;
        }
        while (dirent* entry = readdir(inner)) {
            int index = 0;
            if (!ParseSegmentName(entry->d_name, &index)) {
                continue;
            }
            // The writer only ever advances its segment index, so anything below it is closed.
            if (meeting.writer_owned && index >= scan->writer_segment) {
                continue;
            }
            meeting.closed_segments.push_back(index);
        }
        closedir(inner);
        std::sort(meeting.closed_segments.begin(), meeting.closed_segments.end());
        std::string meta;
        if (ReadWholeFile(JoinPath(meeting_dir, kMetaFile), &meta)) {
            ParseMetaJson(meta, &meeting.created_unix, &meeting.duration_ms);
        }
        scan->meetings.push_back(std::move(meeting));
    }
    return ESP_OK;
}

struct LoadContext {
    std::string id;
    int segment = 0;
    std::string path;
    uint32_t sample_rate = 16000;
    recording_service::RecordedClipPtr clip;
    size_t sample_count = 0;
};

esp_err_t LoadOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* load = static_cast<LoadContext*>(context);
    load->path = JoinPath(MeetingDir(mount_point, load->id), SegmentName(load->segment));
    const long size = FileSize(load->path);
    if (size < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    // Trust the file size rather than the header, which is a placeholder until close.
    load->sample_count = size > static_cast<long>(kWavHeaderBytes)
                             ? static_cast<size_t>(size - static_cast<long>(kWavHeaderBytes)) /
                                   sizeof(int16_t)
                             : 0;
    if (SamplesToMs(load->sample_count, load->sample_rate) < kMinSegmentMs) {
        return ESP_OK;  // caller discards it
    }
    FILE* file = std::fopen(load->path.c_str(), "rb");
    if (file == nullptr) {
        return ESP_FAIL;
    }
    if (std::fseek(file, static_cast<long>(kWavHeaderBytes), SEEK_SET) != 0) {
        std::fclose(file);
        return ESP_FAIL;
    }
    recording_service::RecordedClip::ChunkList chunks;
    size_t remaining = load->sample_count;
    size_t loaded = 0;
    while (remaining > 0) {
        const size_t n = std::min(remaining, kLoadChunkSamples);
        recording_service::RecordedClip::Chunk chunk(n);
        const size_t got = std::fread(chunk.data(), sizeof(int16_t), n, file);
        if (got == 0) {
            break;
        }
        chunk.resize(got);
        chunks.push_back(std::move(chunk));
        loaded += got;
        remaining -= got;
        if (got < n) {
            break;
        }
    }
    std::fclose(file);
    load->sample_count = loaded;
    load->clip = std::make_shared<recording_service::RecordedClip>(load->sample_rate, loaded,
                                                                    std::move(chunks));
    return ESP_OK;
}

struct CommitSegmentContext {
    std::string id;
    int segment = 0;
    std::string text;  // appended to transcript.txt; may be empty (silence)
    bool park = false;  // move the WAV to meetings/failed instead of deleting it
};

esp_err_t CommitSegmentOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* commit = static_cast<CommitSegmentContext*>(context);
    const std::string dir = MeetingDir(mount_point, commit->id);
    const std::string wav = JoinPath(dir, SegmentName(commit->segment));
    if (!commit->text.empty()) {
        const std::string transcript = JoinPath(dir, kTranscriptFile);
        const bool needs_separator = FileSize(transcript) > 0;
        if (!WriteWholeFile(transcript, (needs_separator ? "\n\n" : "") + commit->text, "ab")) {
            return ESP_FAIL;
        }
    }
    if (commit->park) {
        const std::string failed_root = JoinPath(MeetingsRoot(mount_point), kFailedDir);
        if (EnsureDir(failed_root)) {
            const std::string target =
                JoinPath(failed_root, commit->id + "_" + SegmentName(commit->segment));
            if (std::rename(wav.c_str(), target.c_str()) == 0) {
                return ESP_OK;
            }
        }
        ESP_LOGW(kTag, "Could not park %s; leaving it in place", wav.c_str());
        return ESP_FAIL;
    }
    if (unlink(wav.c_str()) != 0 && errno != ENOENT) {
        ESP_LOGW(kTag, "Delete %s failed: errno=%d", wav.c_str(), errno);
    }
    return ESP_OK;
}

struct FinalizeContext {
    std::string id;
    std::string header;
    bool has_text = false;
    std::string note_path;
};

esp_err_t PrepareNoteOnMountedFilesystem(const char* mount_point, void* context)
{
    auto* finalize = static_cast<FinalizeContext*>(context);
    const std::string dir = MeetingDir(mount_point, finalize->id);
    std::string text;
    (void)ReadWholeFile(JoinPath(dir, kTranscriptFile), &text);
    const bool has_text = text.find_first_not_of(" \t\r\n") != std::string::npos;
    finalize->has_text = has_text;
    if (!has_text) {
        return ESP_OK;
    }
    finalize->note_path = JoinPath(dir, kNoteFile);
    return WriteWholeFile(finalize->note_path, finalize->header + "\n\n" + text) ? ESP_OK
                                                                                 : ESP_FAIL;
}

esp_err_t RemoveMeetingOnMountedFilesystem(const char* mount_point, void* context)
{
    const auto* id = static_cast<const std::string*>(context);
    const std::string dir = MeetingDir(mount_point, *id);
    DIR* handle = opendir(dir.c_str());
    if (handle != nullptr) {
        std::vector<std::string> names;
        while (dirent* entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                names.emplace_back(entry->d_name);
            }
        }
        closedir(handle);
        for (const std::string& name : names) {
            (void)unlink(JoinPath(dir, name).c_str());
        }
    }
    if (rmdir(dir.c_str()) != 0 && errno != ENOENT) {
        ESP_LOGW(kTag, "Remove %s failed: errno=%d", dir.c_str(), errno);
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------------------
// Writer task
// ---------------------------------------------------------------------------------------

void DrainRing()
{
    while (s_accum_len < s_accum.size()) {
        const size_t got =
            recording_service::ReadStream(s_accum.data() + s_accum_len, s_accum.size() - s_accum_len);
        if (got == 0) {
            break;
        }
        s_accum_len += got;
        std::lock_guard<std::mutex> lock(s_mutex);
        s_samples_total += got;
    }
}

void RequestStopFromWriter(const char* reason)
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_active) {
            return;
        }
        s_active = false;
        s_stop_requested = true;
        s_last_error = reason;
    }
    ESP_LOGW(kTag, "Stopping meeting: %s", reason);
    (void)recording_service::StopStreaming();
    Emit(EventType::kError);
}

void CheckFreeSpace()
{
    storage_service::StorageStats stats = {};
    if (storage_service::GetStorageStats(&stats) && stats.free_bytes < kMinFreeBytes) {
        RequestStopFromWriter("SD card is full");
    }
}

// Writes buffered audio into the current segment, rolling to the next one at the boundary.
// `final` also closes the open segment once everything is written.
void FlushAccum(bool final)
{
    const size_t segment_capacity = static_cast<size_t>(kSegmentSeconds) * s_sample_rate;
    size_t offset = 0;
    std::string id;
    int segment = 0;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        id = s_meeting_id;
        segment = s_current_segment;
    }

    while (offset < s_accum_len || (final && s_segment_written > 0)) {
        const size_t remaining = s_accum_len - offset;
        const size_t room = segment_capacity - s_segment_written;
        const size_t count = std::min(remaining, room);
        const bool fills_segment = (s_segment_written + count) >= segment_capacity;
        const bool close = fills_segment || (final && count == remaining);

        AppendAudioContext append = {};
        append.id = id;
        append.segment = segment;
        append.samples = s_accum.data() + offset;
        append.sample_count = count;
        append.close_segment = close;
        append.sample_rate = s_sample_rate;
        if (close) {
            std::lock_guard<std::mutex> lock(s_mutex);
            append.meta = BuildMetaJson(s_created_unix, SamplesToMs(s_samples_total, s_sample_rate),
                                        final && count == remaining);
        }
        const esp_err_t err =
            storage_service::RunWithMountedFilesystem(&AppendAudioOnMountedFilesystem, &append);
        if (err != ESP_OK) {
            ++s_write_failures;
            ESP_LOGW(kTag, "Segment write failed (%d/%d): %s", s_write_failures, kMaxWriteFailures,
                     esp_err_to_name(err));
            if (s_write_failures >= kMaxWriteFailures) {
                RequestStopFromWriter("Couldn't write to the SD card");
                // Drop the buffered audio; the segments already on the card are kept.
                s_accum_len = 0;
                return;
            }
            break;
        }
        s_write_failures = 0;
        if (count > 0 && s_segment_written == 0) {
            std::lock_guard<std::mutex> lock(s_mutex);
            ++s_segments_recorded;
        }
        s_segment_written += count;
        offset += count;
        if (close) {
            const bool had_audio = s_segment_written > 0;
            s_segment_written = 0;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                ++s_current_segment;
                segment = s_current_segment;
                if (had_audio) {
                    ++s_segments_pending;
                }
            }
            ESP_LOGI(kTag, "Closed segment %d of meeting %s", segment - 1, id.c_str());
            if (s_transcriber_task != nullptr) {
                xTaskNotifyGive(s_transcriber_task);
            }
            if (!final) {
                CheckFreeSpace();
            }
            Emit(EventType::kProgress);
        }
        if (final && offset >= s_accum_len && s_segment_written == 0) {
            break;
        }
    }

    if (offset > 0) {
        const size_t left = s_accum_len - offset;
        if (left > 0) {
            std::memmove(s_accum.data(), s_accum.data() + offset, left * sizeof(int16_t));
        }
        s_accum_len = left;
    }
}

void WriterTask(void*)
{
    size_t last_dropped = 0;
    for (;;) {
        bool busy = false;
        bool stop = false;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            busy = s_writer_busy;
            stop = s_stop_requested;
        }
        if (!busy) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        DrainRing();
        const size_t dropped = recording_service::StreamDroppedSamples();
        if (dropped != last_dropped) {
            ESP_LOGW(kTag, "Capture ring overflowed; %u samples dropped so far",
                     static_cast<unsigned>(dropped));
            last_dropped = dropped;
        }

        if (stop) {
            DrainRing();
            FlushAccum(true);
            std::string id;
            {
                std::lock_guard<std::mutex> lock(s_mutex);
                id = s_meeting_id;
                s_writer_busy = false;
                s_stop_requested = false;
                s_writer_dir.clear();
            }
            s_accum_len = 0;
            last_dropped = 0;
            ESP_LOGI(kTag, "Meeting %s capture finished", id.c_str());
            if (s_transcriber_task != nullptr) {
                xTaskNotifyGive(s_transcriber_task);
            }
            Emit(EventType::kProgress);
            continue;
        }

        if (s_accum_len >= static_cast<size_t>(kFlushSeconds) * s_sample_rate) {
            FlushAccum(false);
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kDrainIntervalMs));
    }
}

// ---------------------------------------------------------------------------------------
// Transcriber task
// ---------------------------------------------------------------------------------------

std::string BuildNoteHeader(int64_t created_unix, uint32_t duration_ms)
{
    std::string header = "Meeting";
    if (created_unix > 1600000000) {
        const time_t t = static_cast<time_t>(created_unix);
        struct tm local = {};
        localtime_r(&t, &local);
        char date[48] = {};
        std::strftime(date, sizeof(date), " - %a %d %b %Y, %H:%M", &local);
        header += date;
    }
    const uint32_t minutes = (duration_ms + 30000U) / 60000U;
    char length[64] = {};
    if (minutes >= 60) {
        std::snprintf(length, sizeof(length), " (%luh %02lum)",
                      static_cast<unsigned long>(minutes / 60),
                      static_cast<unsigned long>(minutes % 60));
    } else {
        std::snprintf(length, sizeof(length), " (%lu min)",
                      static_cast<unsigned long>(std::max<uint32_t>(minutes, 1)));
    }
    header += length;
    return header;
}

void AppendTail(const std::string& id, const std::string& text)
{
    if (text.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    if (id != s_meeting_id) {
        return;
    }
    if (!s_tail.empty()) {
        s_tail += " ";
    }
    s_tail += text;
    if (s_tail.size() > kTailChars) {
        size_t cut = s_tail.size() - kTailChars;
        // Don't start mid-way through a UTF-8 sequence or word.
        while (cut < s_tail.size() && (static_cast<uint8_t>(s_tail[cut]) & 0xC0) == 0x80) {
            ++cut;
        }
        const size_t space = s_tail.find(' ', cut);
        if (space != std::string::npos && space - cut < 40) {
            cut = space + 1;
        }
        s_tail.erase(0, cut);
    }
}

void SetWaiting(bool waiting)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        changed = s_waiting_for_network != waiting;
        s_waiting_for_network = waiting;
    }
    if (changed) {
        Emit(EventType::kProgress);
    }
}

uint32_t RecordFailure(const std::string& key, const std::string& message, bool* park)
{
    *park = false;
    // Only count failures that happened while Gemini still looked reachable; a dropped
    // connection is not the segment's fault.
    const bool counted = GeminiReady();
    if (key != s_fail_key) {
        s_fail_key = key;
        s_fail_count = 0;
    }
    if (counted) {
        ++s_fail_count;
    }
    if (s_fail_count >= kMaxFailuresBeforePark) {
        *park = true;
        s_fail_count = 0;
        return 0;
    }
    const int exponent = std::max(0, std::min(s_fail_count - 1, 4));
    const uint32_t delay = std::min(kRetryMaxMs, kRetryBaseMs << exponent);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_last_error = message;
        s_retry_scheduled = true;
    }
    Emit(EventType::kError);
    return delay;
}

uint32_t TranscribeSegment(const PendingMeeting& meeting, int segment)
{
    const std::string key = meeting.id + "/" + SegmentName(segment);
    LoadContext load = {};
    load.id = meeting.id;
    load.segment = segment;
    load.sample_rate = recording_service::SampleRateHz();
    const esp_err_t load_err =
        storage_service::RunWithMountedFilesystem(&LoadOnMountedFilesystem, &load);
    if (load_err != ESP_OK) {
        ESP_LOGW(kTag, "Load %s failed: %s", key.c_str(), esp_err_to_name(load_err));
        return kStorageRecheckMs;
    }

    CommitSegmentContext commit = {};
    commit.id = meeting.id;
    commit.segment = segment;

    if (load.clip != nullptr) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_transcribing = true;
            s_retry_scheduled = false;
        }
        Emit(EventType::kProgress);

        gemini_service::TranscriptionResult result = {};
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        if (done == nullptr) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_transcribing = false;
            return kRetryBaseMs;
        }
        const recording_service::RecordedClipPtr clip = load.clip;
        load.clip.reset();
        gemini_service::RunOnWorker([&result, clip, done]() {
            result = gemini_service::Transcribe(*clip);
            xSemaphoreGive(done);
        });
        (void)xSemaphoreTake(done, portMAX_DELAY);
        vSemaphoreDelete(done);
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_transcribing = false;
        }
        ESP_LOGI(kTag, "Transcribed %s: ok=%d status=%d bytes=%u elapsed_ms=%llu err=%s",
                 key.c_str(), result.success ? 1 : 0, result.http_status,
                 static_cast<unsigned>(result.transcript.size()),
                 static_cast<unsigned long long>(result.total_elapsed_ms),
                 result.error_code.c_str());

        const bool silent = !result.success && result.error_code == "empty_transcript";
        if (!result.success && !silent) {
            bool park = false;
            const uint32_t delay = RecordFailure(
                key, result.error_message.empty() ? result.error_code : result.error_message,
                &park);
            if (!park) {
                return delay;
            }
            ESP_LOGW(kTag, "Giving up on %s after repeated failures; parking its audio",
                     key.c_str());
            commit.park = true;
            commit.text = "[Part " + std::to_string(segment) +
                          " couldn't be transcribed - its audio is kept on the SD card in "
                          "meetings/failed]";
        } else {
            commit.text = result.transcript;
        }
    }

    const esp_err_t commit_err =
        storage_service::RunWithMountedFilesystem(&CommitSegmentOnMountedFilesystem, &commit);
    if (commit_err != ESP_OK) {
        ESP_LOGW(kTag, "Commit %s failed: %s", key.c_str(), esp_err_to_name(commit_err));
        return kStorageRecheckMs;
    }
    s_fail_key.clear();
    s_fail_count = 0;
    AppendTail(meeting.id, commit.text);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (meeting.id == s_meeting_id) {
            ++s_segments_transcribed;
        }
        s_segments_pending = std::max(0, s_segments_pending - 1);
        s_retry_scheduled = false;
        s_last_error.clear();
    }
    Emit(EventType::kProgress);
    return 0;
}

uint32_t FinalizeMeeting(const PendingMeeting& meeting)
{
    FinalizeContext finalize = {};
    finalize.id = meeting.id;
    finalize.header = BuildNoteHeader(meeting.created_unix, meeting.duration_ms);
    esp_err_t err =
        storage_service::RunWithMountedFilesystem(&PrepareNoteOnMountedFilesystem, &finalize);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Preparing note for %s failed: %s", meeting.id.c_str(), esp_err_to_name(err));
        return kStorageRecheckMs;
    }

    std::string saved_id;
    if (finalize.has_text) {
        recording_archive_service::SaveOptions options = {};
        options.tag = recording_archive_service::RecordingTag::kNote;
        const int64_t created =
            meeting.created_unix > 0 ? meeting.created_unix : static_cast<int64_t>(std::time(nullptr));
        const recording_archive_service::SaveResult result =
            recording_archive_service::SaveTextNoteFromFile(finalize.note_path, created,
                                                            meeting.duration_ms, options);
        if (!result.success) {
            ESP_LOGW(kTag, "Saving meeting note %s failed: %s", meeting.id.c_str(),
                     result.error_message.c_str());
            bool park = false;
            return std::max(RecordFailure(meeting.id, "Couldn't save the meeting note", &park),
                            kRetryBaseMs);
        }
        saved_id = result.recording_id;
        ESP_LOGI(kTag, "Meeting %s saved as note %s", meeting.id.c_str(), saved_id.c_str());
    } else {
        ESP_LOGI(kTag, "Meeting %s had no transcribed speech; nothing to save",
                 meeting.id.c_str());
    }

    std::string id = meeting.id;
    (void)storage_service::RunWithMountedFilesystem(&RemoveMeetingOnMountedFilesystem, &id);
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (meeting.id == s_meeting_id) {
            s_saved_id = saved_id;
        }
        s_last_error = saved_id.empty() ? "No speech was detected" : "";
        s_retry_scheduled = false;
    }
    Emit(EventType::kSaved);
    return 0;
}

uint32_t TranscriberStep()
{
    if (!StorageUsable()) {
        return kStorageRecheckMs;
    }
    ScanContext scan = {};
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        scan.writer_dir = s_writer_dir;
        scan.writer_segment = s_current_segment;
    }
    const esp_err_t err = storage_service::RunWithMountedFilesystem(&ScanOnMountedFilesystem, &scan);
    if (err != ESP_OK) {
        return kStorageRecheckMs;
    }

    int pending = 0;
    const PendingMeeting* transcribe = nullptr;
    const PendingMeeting* finalize = nullptr;
    for (const PendingMeeting& meeting : scan.meetings) {
        pending += static_cast<int>(meeting.closed_segments.size());
        if (transcribe == nullptr && !meeting.closed_segments.empty()) {
            transcribe = &meeting;
        }
        if (finalize == nullptr && !meeting.writer_owned && meeting.closed_segments.empty()) {
            finalize = &meeting;
        }
    }
    bool pending_changed = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        // Re-read the writer: a segment it closed during the scan isn't counted yet.
        pending_changed = s_segments_pending != pending;
        s_segments_pending = pending;
    }
    if (pending_changed) {
        Emit(EventType::kProgress);
    }

    if (finalize != nullptr) {
        return FinalizeMeeting(*finalize);
    }
    if (transcribe == nullptr) {
        SetWaiting(false);
        return kWaitForever;
    }
    if (!GeminiReady()) {
        SetWaiting(true);
        return kWaitingRecheckMs;
    }
    SetWaiting(false);
    return TranscribeSegment(*transcribe, transcribe->closed_segments.front());
}

void TranscriberTask(void*)
{
    // Let boot settle (SD mount, Wi-Fi, Gemini auth) before looking for leftover meetings.
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kBootScanDelayMs));
    for (;;) {
        const uint32_t wait_ms = TranscriberStep();
        if (wait_ms == 0) {
            continue;
        }
        (void)ulTaskNotifyTake(pdTRUE,
                               wait_ms == kWaitForever ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms));
        std::lock_guard<std::mutex> lock(s_mutex);
        s_retry_scheduled = false;
    }
}

}  // namespace

esp_err_t Init()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_initialized) {
            return ESP_OK;
        }
        s_initialized = true;
    }
    if (xTaskCreatePinnedToCore(&WriterTask, "meeting_writer", kWriterStackWords, nullptr,
                                chroninkle_task_config::kPriorityMeetingWriter, &s_writer_task,
                                chroninkle_task_config::kAppCore) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create writer task");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(&TranscriberTask, "meeting_stt", kTranscriberStackWords, nullptr,
                                chroninkle_task_config::kPriorityMeetingTranscribe,
                                &s_transcriber_task,
                                chroninkle_task_config::kSystemCore) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create transcriber task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "Meeting service ready: segment_s=%lu",
             static_cast<unsigned long>(kSegmentSeconds));
    return ESP_OK;
}

void SetEventHandler(EventHandler handler, void* context)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_event_handler = handler;
    s_event_context = context;
}

Snapshot GetSnapshot()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return BuildSnapshotLocked();
}

esp_err_t Start()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized) {
            return ESP_ERR_INVALID_STATE;
        }
        if (s_active || s_writer_busy) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (!StorageUsable()) {
        return ESP_ERR_NOT_FOUND;
    }
    storage_service::StorageStats stats = {};
    if (storage_service::GetStorageStats(&stats) && stats.free_bytes < kMinFreeBytes) {
        return ESP_ERR_NO_MEM;
    }
    const recording_service::UiState recorder = recording_service::GetUiState();
    if (recorder.armed || recorder.recording) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t now_unix = static_cast<int64_t>(std::time(nullptr));
    char id[64] = {};
    std::snprintf(id, sizeof(id), "mtg_%010lld_%06lld",
                  static_cast<long long>(now_unix > 0 ? now_unix : 0),
                  static_cast<long long>((esp_timer_get_time() / 1000) % 1000000));
    const std::string meeting_id = id;

    // Claim the folder for the writer before it exists, so a concurrent transcriber scan
    // can't mistake the fresh, segment-less meeting for a finished one and publish it.
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_writer_dir = meeting_id;
        s_current_segment = 1;
    }

    StartContext start = {};
    start.id = &meeting_id;
    start.meta = BuildMetaJson(now_unix, 0, false);
    esp_err_t err = storage_service::RunWithMountedFilesystem(&StartOnMountedFilesystem, &start);
    if (err != ESP_OK) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_writer_dir.clear();
        }
        ESP_LOGW(kTag, "Creating meeting folder failed: %s", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    const uint32_t rate = recording_service::SampleRateHz();
    if (s_accum.size() != static_cast<size_t>(kAccumSeconds) * rate) {
        s_accum.assign(static_cast<size_t>(kAccumSeconds) * rate, 0);
    }
    s_accum_len = 0;
    s_segment_written = 0;
    s_write_failures = 0;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_meeting_id = meeting_id;
        s_samples_total = 0;
        s_sample_rate = rate;
        s_created_unix = now_unix;
        s_segments_recorded = 0;
        s_segments_transcribed = 0;
        s_tail.clear();
        s_saved_id.clear();
        s_last_error.clear();
        s_stop_requested = false;
        s_active = true;
        s_writer_busy = true;
    }

    err = recording_service::StartStreaming();
    if (err != ESP_OK) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_active = false;
            s_writer_busy = false;
            s_writer_dir.clear();
        }
        std::string doomed = meeting_id;
        (void)storage_service::RunWithMountedFilesystem(&RemoveMeetingOnMountedFilesystem, &doomed);
        ESP_LOGW(kTag, "Starting capture failed: %s", esp_err_to_name(err));
        return err == ESP_ERR_INVALID_STATE ? err : ESP_FAIL;
    }

    ESP_LOGI(kTag, "Meeting %s started", meeting_id.c_str());
    xTaskNotifyGive(s_writer_task);
    Emit(EventType::kStarted);
    return ESP_OK;
}

esp_err_t Stop()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_active) {
            return ESP_ERR_INVALID_STATE;
        }
        s_active = false;
        s_stop_requested = true;
    }
    (void)recording_service::StopStreaming();
    if (s_writer_task != nullptr) {
        xTaskNotifyGive(s_writer_task);
    }
    ESP_LOGI(kTag, "Meeting stop requested");
    Emit(EventType::kStopped);
    return ESP_OK;
}

void StopForShutdown()
{
    (void)Stop();
    const int64_t deadline_us = esp_timer_get_time() + kShutdownFlushWaitMs * 1000LL;
    while (esp_timer_get_time() < deadline_us) {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_writer_busy) {
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(kTag, "Meeting flush did not finish before shutdown");
}

bool IsActive()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_active;
}

bool IsTranscribing()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_transcribing || (s_writer_busy && !s_active)) {
        return true;
    }
    return s_segments_pending > 0 && !s_waiting_for_network && !s_retry_scheduled;
}

void Kick()
{
    if (s_transcriber_task != nullptr) {
        xTaskNotifyGive(s_transcriber_task);
    }
}

}  // namespace meeting_service
