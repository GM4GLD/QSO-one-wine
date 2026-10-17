#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propidl.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

DEFINE_GUID(MY_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);
DEFINE_GUID(MY_SUBTYPE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71);

static double peak_of_buffer(const BYTE *data, UINT32 frames, const WAVEFORMATEX *fmt)
{
    WORD tag = fmt->wFormatTag;
    WORD bits = fmt->wBitsPerSample;
    WORD ch = fmt->nChannels;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *x = (const WAVEFORMATEXTENSIBLE *)fmt;
        if (IsEqualGUID(&x->SubFormat, &MY_SUBTYPE_FLOAT)) tag = 3;
        else if (IsEqualGUID(&x->SubFormat, &MY_SUBTYPE_PCM)) tag = 1;
    }
    double peak = 0.0;
    UINT32 i, n = frames * ch;
    if (tag == 3 && bits == 32) {
        const float *p = (const float *)data;
        for (i = 0; i < n; i++) { double a = p[i] < 0 ? -p[i] : p[i]; if (a > peak) peak = a; }
    } else if (tag == 1 && bits == 16) {
        const short *p = (const short *)data;
        for (i = 0; i < n; i++) { double a = (p[i] < 0 ? -p[i] : p[i]) / 32768.0; if (a > peak) peak = a; }
    } else if (tag == 1 && bits == 32) {
        const int *p = (const int *)data;
        for (i = 0; i < n; i++) { double a = (double)p[i] / 2147483648.0; if (a < 0) a = -a; if (a > peak) peak = a; }
    } else {
        return -1.0;
    }
    return peak;
}

static void test_wasapi(void)
{
    HRESULT hr;
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *client = NULL;
    IAudioCaptureClient *cap = NULL;
    WAVEFORMATEX *fmt = NULL;
    UINT32 bufsize = 0;
    int i;

    printf("=== WASAPI capture test ===\n");
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                        &IID_IMMDeviceEnumerator, (void **)&en);
    printf("CoCreateInstance(MMDeviceEnumerator): 0x%08lx\n", hr);
    if (FAILED(hr)) return;

    {
        IMMDeviceCollection *col = NULL;
        UINT32 count = 0;
        hr = IMMDeviceEnumerator_EnumAudioEndpoints(en, eCapture, DEVICE_STATE_ACTIVE, &col);
        if (SUCCEEDED(hr)) { IMMDeviceCollection_GetCount(col, &count); IMMDeviceCollection_Release(col); }
        printf("EnumAudioEndpoints(eCapture,ACTIVE): hr=0x%08lx count=%lu\n", hr, count);
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eCapture, eConsole, &dev);
    printf("GetDefaultAudioEndpoint(eCapture): 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    {
        IPropertyStore *ps = NULL; PROPVARIANT v;
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps))) {
            PropVariantInit(&v);
            if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &v)))
                printf("  device: %ls\n", v.pwszVal);
            PropVariantClear(&v);
            IPropertyStore_Release(ps);
        }
    }

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client);
    printf("Activate(IAudioClient): 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IAudioClient_GetMixFormat(client, &fmt);
    printf("GetMixFormat: 0x%08lx tag=%u rate=%lu ch=%u bits=%u\n", hr,
           fmt ? fmt->wFormatTag : 0, fmt ? fmt->nSamplesPerSec : 0,
           fmt ? fmt->nChannels : 0, fmt ? fmt->wBitsPerSample : 0);
    if (FAILED(hr)) goto done;

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, fmt, NULL);
    printf("Initialize(shared,poll,100ms): 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IAudioClient_GetBufferSize(client, &bufsize);
    printf("GetBufferSize: 0x%08lx (%lu frames)\n", hr, bufsize);

    hr = IAudioClient_GetService(client, &IID_IAudioCaptureClient, (void **)&cap);
    printf("GetService(IAudioCaptureClient): 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    printf("Speak now -- capturing 2 seconds...\n");
    hr = IAudioClient_Start(client);
    printf("Start: 0x%08lx\n", hr);
    {
        double peak = 0.0; UINT64 total = 0; int silent_bufs = 0, bufcount = 0;
        for (i = 0; i < 60; i++) {
            UINT32 packet = 0;
            IAudioCaptureClient_GetNextPacketSize(cap, &packet);
            while (packet) {
                BYTE *data = NULL; UINT32 frames = 0; DWORD flags = 0;
                hr = IAudioCaptureClient_GetBuffer(cap, &data, &frames, &flags, NULL, NULL);
                if (FAILED(hr)) { printf("GetBuffer: 0x%08lx\n", hr); break; }
                bufcount++;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) silent_bufs++;
                else { double p = peak_of_buffer(data, frames, fmt); if (p > peak) peak = p; }
                total += frames;
                IAudioCaptureClient_ReleaseBuffer(cap, frames);
                IAudioCaptureClient_GetNextPacketSize(cap, &packet);
            }
            Sleep(33);
        }
        IAudioClient_Stop(client);
        printf("RESULT: buffers=%d silent=%d frames=%llu peak=%.6f %s\n",
               bufcount, silent_bufs, (unsigned long long)total, peak,
               peak > 0.001 ? "=> AUDIO OK" : "=> EFFECTIVELY SILENT");
    }

done:
    if (cap) IAudioCaptureClient_Release(cap);
    if (client) IAudioClient_Release(client);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
    if (fmt) CoTaskMemFree(fmt);
}

static void test_mf(void)
{
    HRESULT hr;
    IMFAttributes *attrs = NULL;
    IMFActivate **acts = NULL;
    UINT32 count = 0, i;
    IMFMediaSource *ms = NULL;
    IMFSourceReader *reader = NULL;

    printf("\n=== Media Foundation audcap test ===\n");
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    printf("MFStartup: 0x%08lx\n", hr);
    if (FAILED(hr)) return;

    hr = MFCreateAttributes(&attrs, 1);
    IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                          &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    hr = MFEnumDeviceSources(attrs, &acts, &count);
    printf("MFEnumDeviceSources(AUDCAP): hr=0x%08lx count=%lu\n", hr, count);
    for (i = 0; i < count; i++) {
        WCHAR *name = NULL, *link = NULL; UINT32 len = 0;
        IMFActivate_GetAllocatedString(acts[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
        IMFActivate_GetAllocatedString(acts[i], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_SYMBOLIC_LINK, &link, &len);
        printf("  [%lu] %ls\n      link=%ls\n", i, name ? name : L"?", link ? link : L"?");
        CoTaskMemFree(name); CoTaskMemFree(link);
    }
    if (!count) goto done;

    /* pick "PulseAudio Input" (default mic) if present, else index 1 (physical) */
    {
        UINT32 pick = 0, k;
        for (k = 0; k < count; k++) {
            WCHAR *nm = NULL; UINT32 l = 0;
            if (SUCCEEDED(IMFActivate_GetAllocatedString(acts[k], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nm, &l)) && nm) {
                if (wcsstr(nm, L"PulseAudio Input")) pick = k;
                CoTaskMemFree(nm);
            }
        }
        printf("using activate index %lu\n", pick);
        hr = MFCreateDeviceSource((IMFAttributes *)acts[pick], &ms);
    }
    printf("MFCreateDeviceSource: 0x%08lx\n", hr);
    if (FAILED(hr)) {
        printf("trying IMFActivate::ActivateObject(IID_IMFMediaSource)...\n");
        hr = IMFActivate_ActivateObject(acts[0], &IID_IMFMediaSource, (void **)&ms);
        printf("ActivateObject: 0x%08lx\n", hr);
    }
    if (FAILED(hr)) goto done;

    hr = MFCreateSourceReaderFromMediaSource(ms, NULL, &reader);
    printf("MFCreateSourceReaderFromMediaSource: 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    {
        IMFMediaType *mt = NULL;
        MFCreateMediaType(&mt);
        IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt);
        printf("SetCurrentMediaType(PCM): 0x%08lx\n", hr);
        IMFMediaType_Release(mt);

        IMFMediaType *cur = NULL;
        if (SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur))) {
            UINT32 v = 0;
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &v);
            printf("  current rate=%lu\n", v);
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_NUM_CHANNELS, &v);
            printf("  current ch=%lu\n", v);
            IMFMediaType_GetUINT32(cur, &MF_MT_AUDIO_BITS_PER_SAMPLE, &v);
            printf("  current bits=%lu\n", v);
            IMFMediaType_Release(cur);
        }
    }

    printf("Speak now -- reading 2 seconds via IMFSourceReader...\n");
    {
        double peak = 0.0; UINT64 bytes = 0; int samples = 0;
        DWORD stream_flags = 0; LONGLONG ts = 0; DWORD actual = 0;
        IMFSample *sample = NULL;
        DWORD deadline = GetTickCount() + 2000;
        for (;;) {
            hr = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                            &actual, &stream_flags, &ts, &sample);
            if (FAILED(hr)) { printf("ReadSample: 0x%08lx\n", hr); break; }
            if (sample) {
                IMFMediaBuffer *buf = NULL;
                samples++;
                if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buf))) {
                    BYTE *data = NULL; DWORD maxlen = 0, curlen = 0;
                    IMFMediaBuffer_Lock(buf, &data, &maxlen, &curlen);
                    bytes += curlen;
                    {
                        const short *p = (const short *)data; DWORD n = curlen / 2, k;
                        for (k = 0; k < n; k++) { double a = (p[k] < 0 ? -p[k] : p[k]) / 32768.0; if (a > peak) peak = a; }
                    }
                    IMFMediaBuffer_Unlock(buf);
                    IMFMediaBuffer_Release(buf);
                }
                IMFSample_Release(sample); sample = NULL;
            }
            if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (GetTickCount() > deadline) break;
        }
        printf("RESULT: samples=%d bytes=%llu peak=%.6f %s\n", samples,
               (unsigned long long)bytes, peak,
               peak > 0.001 ? "=> AUDIO OK" : "=> EFFECTIVELY SILENT");
    }

done:
    if (reader) IMFSourceReader_Release(reader);
    if (ms) IMFMediaSource_Release(ms);
    for (i = 0; i < count; i++) IMFActivate_Release(acts[i]);
    CoTaskMemFree(acts);
    if (attrs) IMFAttributes_Release(attrs);
    MFShutdown();
}

int main(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("CoInitializeEx: 0x%08lx\n", hr);
    test_wasapi();
    test_mf();
    CoUninitialize();
    return 0;
}
