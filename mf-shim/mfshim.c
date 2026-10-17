/*
 * mf.dll shim for Wine — implements MFEnumDeviceSources / MFCreateDeviceSource /
 * MFCreateDeviceSourceActivate for *audio capture* devices, backed directly by
 * WASAPI (mmdevapi/IAudioCaptureClient), which works under Wine's winepulse.drv.
 *
 * Wine 11.x stubs MFCreateDeviceSource (E_NOTIMPL) and its
 * audio_capture_create_object.  Only record_windows_plugin.dll in QSO One
 * imports mf.dll, so placing this shim next to qso_one.exe scopes the
 * workaround to that one app.  Deleting mf.dll restores stock behaviour.
 */
#define COBJMACROS
#define INITGUID
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mferror.h>
#include <devpkey.h>
#include <functiondiscoverykeys_devpkey.h>

#define LOG(...) do { fprintf(stderr, "[mfshim] " __VA_ARGS__); fflush(stderr); } while (0)

/* ------------------------------------------------------------------ */
/* Generic IUnknown helpers                                            */
/* ------------------------------------------------------------------ */

#define DEF_QI(name, structname, member, iid)                                \
static HRESULT WINAPI name##_QI(void *iface, REFIID riid, void **obj)        \
{                                                                          \
    structname *o = CONTAINING_RECORD(iface, structname, member);          \
    if (IsEqualIID(riid, iid) || IsEqualIID(riid, &IID_IUnknown))            \
    {                                                                      \
        *obj = iface;                                                      \
        IUnknown_AddRef((IUnknown *)iface);                                \
        return S_OK;                                                       \
    }                                                                      \
    *obj = NULL;                                                           \
    return E_NOINTERFACE;                                                  \
}

#define DEF_REFS(name, structname, member, releasefn)                        \
static ULONG WINAPI name##_AddRef(void *iface)                               \
{                                                                          \
    structname *o = CONTAINING_RECORD(iface, structname, member);          \
    return InterlockedIncrement(&o->refcount);                             \
}                                                                          \
static ULONG WINAPI name##_Release(void *iface)                              \
{                                                                          \
    structname *o = CONTAINING_RECORD(iface, structname, member);          \
    ULONG r = InterlockedDecrement(&o->refcount);                            \
    if (!r) releasefn(o);                                                  \
    return r;                                                              \
}

/* Delegate every IMFAttributes method to an inner object.
 * name##_pre delegates are placed in the vtable where the inherited
 * IMFAttributes methods live. */
#define DEF_ATTR_DELEGATES(pfx, objt, ifacem, innerm)                        \
static HRESULT WINAPI pfx##_GetItem(IMFAttributes *i, REFGUID k, PROPVARIANT *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetItem(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetItemType(IMFAttributes *i, REFGUID k, MF_ATTRIBUTE_TYPE *t){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetItemType(o->innerm,k,t);} \
static HRESULT WINAPI pfx##_CompareItem(IMFAttributes *i, REFGUID k, REFPROPVARIANT v, BOOL *r){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_CompareItem(o->innerm,k,v,r);} \
static HRESULT WINAPI pfx##_Compare(IMFAttributes *i, IMFAttributes *t, MF_ATTRIBUTES_MATCH_TYPE mt, BOOL *r){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_Compare(o->innerm,t,mt,r);} \
static HRESULT WINAPI pfx##_GetUINT32(IMFAttributes *i, REFGUID k, UINT32 *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetUINT32(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetUINT64(IMFAttributes *i, REFGUID k, UINT64 *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetUINT64(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetDouble(IMFAttributes *i, REFGUID k, double *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetDouble(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetGUID(IMFAttributes *i, REFGUID k, GUID *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetGUID(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetStringLength(IMFAttributes *i, REFGUID k, UINT32 *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetStringLength(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetString(IMFAttributes *i, REFGUID k, LPWSTR v, UINT32 s, UINT32 *l){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetString(o->innerm,k,v,s,l);} \
static HRESULT WINAPI pfx##_GetAllocatedString(IMFAttributes *i, REFGUID k, LPWSTR *v, UINT32 *l){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetAllocatedString(o->innerm,k,v,l);} \
static HRESULT WINAPI pfx##_GetBlobSize(IMFAttributes *i, REFGUID k, UINT32 *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetBlobSize(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_GetBlob(IMFAttributes *i, REFGUID k, UINT8 *v, UINT32 s, UINT32 *l){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetBlob(o->innerm,k,v,s,l);} \
static HRESULT WINAPI pfx##_GetAllocatedBlob(IMFAttributes *i, REFGUID k, UINT8 **v, UINT32 *l){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetAllocatedBlob(o->innerm,k,v,l);} \
static HRESULT WINAPI pfx##_GetUnknown(IMFAttributes *i, REFGUID k, REFIID r, LPVOID *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetUnknown(o->innerm,k,r,v);} \
static HRESULT WINAPI pfx##_SetItem(IMFAttributes *i, REFGUID k, REFPROPVARIANT v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetItem(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_DeleteItem(IMFAttributes *i, REFGUID k){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_DeleteItem(o->innerm,k);} \
static HRESULT WINAPI pfx##_DeleteAllItems(IMFAttributes *i){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_DeleteAllItems(o->innerm);} \
static HRESULT WINAPI pfx##_SetUINT32(IMFAttributes *i, REFGUID k, UINT32 v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetUINT32(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_SetUINT64(IMFAttributes *i, REFGUID k, UINT64 v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetUINT64(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_SetDouble(IMFAttributes *i, REFGUID k, double v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetDouble(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_SetGUID(IMFAttributes *i, REFGUID k, REFGUID v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetGUID(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_SetString(IMFAttributes *i, REFGUID k, LPCWSTR v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetString(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_SetBlob(IMFAttributes *i, REFGUID k, const UINT8 *v, UINT32 s){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetBlob(o->innerm,k,v,s);} \
static HRESULT WINAPI pfx##_SetUnknown(IMFAttributes *i, REFGUID k, IUnknown *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_SetUnknown(o->innerm,k,v);} \
static HRESULT WINAPI pfx##_LockStore(IMFAttributes *i){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_LockStore(o->innerm);} \
static HRESULT WINAPI pfx##_UnlockStore(IMFAttributes *i){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_UnlockStore(o->innerm);} \
static HRESULT WINAPI pfx##_GetCount(IMFAttributes *i, UINT32 *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetCount(o->innerm,v);} \
static HRESULT WINAPI pfx##_GetItemByIndex(IMFAttributes *i, UINT32 x, GUID *k, PROPVARIANT *v){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_GetItemByIndex(o->innerm,x,k,v);} \
static HRESULT WINAPI pfx##_CopyAllItems(IMFAttributes *i, IMFAttributes *d){objt*o=CONTAINING_RECORD(i,objt,ifacem);return IMFAttributes_CopyAllItems(o->innerm,d);}

#define ATTR_VTABLE_ENTRIES(pfx) (void*)pfx##_GetItem, \
    (void*)pfx##_GetItemType, \
    (void*)pfx##_CompareItem, \
    (void*)pfx##_Compare, \
    (void*)pfx##_GetUINT32, \
    (void*)pfx##_GetUINT64, \
    (void*)pfx##_GetDouble, \
    (void*)pfx##_GetGUID, \
    (void*)pfx##_GetStringLength, \
    (void*)pfx##_GetString, \
    (void*)pfx##_GetAllocatedString, \
    (void*)pfx##_GetBlobSize, \
    (void*)pfx##_GetBlob, \
    (void*)pfx##_GetAllocatedBlob, \
    (void*)pfx##_GetUnknown, \
    (void*)pfx##_SetItem, \
    (void*)pfx##_DeleteItem, \
    (void*)pfx##_DeleteAllItems, \
    (void*)pfx##_SetUINT32, \
    (void*)pfx##_SetUINT64, \
    (void*)pfx##_SetDouble, \
    (void*)pfx##_SetGUID, \
    (void*)pfx##_SetString, \
    (void*)pfx##_SetBlob, \
    (void*)pfx##_SetUnknown, \
    (void*)pfx##_LockStore, \
    (void*)pfx##_UnlockStore, \
    (void*)pfx##_GetCount, \
    (void*)pfx##_GetItemByIndex, \
    (void*)pfx##_CopyAllItems

/* ------------------------------------------------------------------ */
/* Minimal IMFAsyncResult                                              */
/* ------------------------------------------------------------------ */

typedef struct async_result {
    IMFAsyncResult IMFAsyncResult_iface;
    LONG refcount;
    IMFMediaEvent *event;   /* NULL while pending / on failure */
    IUnknown *state;
    HRESULT status;
} async_result;

static void async_result_free(async_result *r)
{
    if (r->event) IMFMediaEvent_Release(r->event);
    if (r->state) IUnknown_Release(r->state);
    free(r);
}
DEF_QI(ares, async_result, IMFAsyncResult_iface, &IID_IMFAsyncResult)
DEF_REFS(ares, async_result, IMFAsyncResult_iface, async_result_free)
static HRESULT WINAPI ares_GetState(IMFAsyncResult *i, IUnknown **s)
{
    async_result *r = CONTAINING_RECORD(i, async_result, IMFAsyncResult_iface);
    if (!r->state) { *s = NULL; return E_POINTER; }
    *s = r->state; IUnknown_AddRef(r->state); return S_OK;
}
static HRESULT WINAPI ares_GetStatus(IMFAsyncResult *i)
{
    async_result *r = CONTAINING_RECORD(i, async_result, IMFAsyncResult_iface);
    return r->status;
}
static HRESULT WINAPI ares_SetStatus(IMFAsyncResult *i, HRESULT st)
{
    async_result *r = CONTAINING_RECORD(i, async_result, IMFAsyncResult_iface);
    r->status = st; return S_OK;
}
static HRESULT WINAPI ares_GetObject(IMFAsyncResult *i, IUnknown **o)
{
    async_result *r = CONTAINING_RECORD(i, async_result, IMFAsyncResult_iface);
    if (!r->event) { *o = NULL; return E_POINTER; }
    return IMFMediaEvent_QueryInterface(r->event, &IID_IUnknown, (void **)o);
}
static IUnknown * WINAPI ares_GetStateNoAddRef(IMFAsyncResult *i)
{
    async_result *r = CONTAINING_RECORD(i, async_result, IMFAsyncResult_iface);
    return r->state;
}
static const IMFAsyncResultVtbl ares_vtbl = {
    (void*)ares_QI, (void*)ares_AddRef, (void*)ares_Release,
    ares_GetState, ares_GetStatus, ares_SetStatus, ares_GetObject,
    ares_GetStateNoAddRef,
};

static async_result *async_result_new(IUnknown *state, IMFMediaEvent *event, HRESULT status)
{
    async_result *r = calloc(1, sizeof(*r));
    r->IMFAsyncResult_iface.lpVtbl = &ares_vtbl;
    r->refcount = 1;
    r->status = status;
    if ((r->event = event)) IMFMediaEvent_AddRef(event);
    if ((r->state = state)) IUnknown_AddRef(state);
    return r;
}

/* ------------------------------------------------------------------ */
/* IMFMediaEvent                                                       */
/* ------------------------------------------------------------------ */

typedef struct media_event {
    IMFMediaEvent IMFMediaEvent_iface;
    LONG refcount;
    IMFAttributes *attrs;
    MediaEventType type;
    GUID ext;
    HRESULT status;
    PROPVARIANT value;
} media_event;

static void media_event_free(media_event *e)
{
    PropVariantClear(&e->value);
    if (e->attrs) IMFAttributes_Release(e->attrs);
    free(e);
}

static HRESULT WINAPI mevt_QI(IMFMediaEvent *iface, REFIID riid, void **obj)
{
    media_event *e = CONTAINING_RECORD(iface, media_event, IMFMediaEvent_iface);
    if (IsEqualIID(riid, &IID_IMFMediaEvent) || IsEqualIID(riid, &IID_IMFAttributes) ||
        IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFMediaEvent_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(mevt, media_event, IMFMediaEvent_iface, media_event_free)
DEF_ATTR_DELEGATES(mevt, media_event, IMFMediaEvent_iface, attrs)
static HRESULT WINAPI mevt_GetType(IMFMediaEvent *i, MediaEventType *t)
{ media_event *e = CONTAINING_RECORD(i, media_event, IMFMediaEvent_iface); *t = e->type; return S_OK; }
static HRESULT WINAPI mevt_GetExtendedType(IMFMediaEvent *i, GUID *g)
{ media_event *e = CONTAINING_RECORD(i, media_event, IMFMediaEvent_iface); *g = e->ext; return S_OK; }
static HRESULT WINAPI mevt_GetStatus(IMFMediaEvent *i, HRESULT *s)
{ media_event *e = CONTAINING_RECORD(i, media_event, IMFMediaEvent_iface); *s = e->status; return S_OK; }
static HRESULT WINAPI mevt_GetValue(IMFMediaEvent *i, PROPVARIANT *v)
{
    media_event *e = CONTAINING_RECORD(i, media_event, IMFMediaEvent_iface);
    PropVariantInit(v);
    *v = e->value;
    if (v->vt == VT_UNKNOWN && v->punkVal) IUnknown_AddRef(v->punkVal);
    return S_OK;
}
static const IMFMediaEventVtbl mevt_vtbl = {
    (void*)mevt_QI, (void*)mevt_AddRef, (void*)mevt_Release,
    ATTR_VTABLE_ENTRIES(mevt),
    mevt_GetType, mevt_GetExtendedType, mevt_GetStatus, mevt_GetValue,
};

static IMFMediaEvent *media_event_new(MediaEventType type, HRESULT status, const PROPVARIANT *value)
{
    media_event *e = calloc(1, sizeof(*e));
    e->IMFMediaEvent_iface.lpVtbl = &mevt_vtbl;
    e->refcount = 1;
    MFCreateAttributes(&e->attrs, 0);
    e->type = type;
    e->ext = GUID_NULL;
    e->status = status;
    PropVariantInit(&e->value);
    if (value) { e->value = *value; if (e->value.vt == VT_UNKNOWN && e->value.punkVal) IUnknown_AddRef(e->value.punkVal); }
    else e->value.vt = VT_EMPTY;
    return &e->IMFMediaEvent_iface;
}

/* ------------------------------------------------------------------ */
/* Event generator (shared by source and stream)                       */
/* ------------------------------------------------------------------ */

typedef struct ev_node { struct ev_node *next; IMFMediaEvent *event; } ev_node;

typedef struct event_gen {
    CRITICAL_SECTION cs;
    ev_node *head, *tail;
    int shutdown;
    async_result *pending;
    IMFAsyncCallback *pending_cb;
} event_gen;

static void eg_init(event_gen *g)
{
    InitializeCriticalSection(&g->cs);
    g->head = g->tail = NULL;
    g->shutdown = 0;
    g->pending = NULL;
    g->pending_cb = NULL;
}
static void eg_destroy(event_gen *g)
{
    ev_node *n = g->head;
    while (n) { ev_node *nx = n->next; IMFMediaEvent_Release(n->event); free(n); n = nx; }
    DeleteCriticalSection(&g->cs);
}

typedef struct { IMFAsyncCallback *cb; async_result *result; } invoke_ctx;

static DWORD WINAPI invoke_worker(void *p)
{
    invoke_ctx *c = p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMFAsyncCallback_Invoke(c->cb, &c->result->IMFAsyncResult_iface);
    IMFAsyncCallback_Release(c->cb);
    IMFAsyncResult_Release(&c->result->IMFAsyncResult_iface);
    CoUninitialize();
    free(c);
    return 0;
}

static void eg_complete_pending(event_gen *g, async_result *result, IMFAsyncCallback *cb)
{
    invoke_ctx *c = malloc(sizeof(*c));
    c->cb = cb;   /* cb ref held from BeginGetEvent */
    c->result = result;
    QueueUserWorkItem(invoke_worker, c, 0);
}

static HRESULT eg_queue(event_gen *g, MediaEventType type, HRESULT status, const PROPVARIANT *value)
{
    IMFMediaEvent *e = media_event_new(type, status, value);
    ev_node *n = malloc(sizeof(*n));
    n->event = e; n->next = NULL;
    EnterCriticalSection(&g->cs);
    if (g->shutdown) { LeaveCriticalSection(&g->cs); IMFMediaEvent_Release(e); free(n); return MF_E_SHUTDOWN; }
    if (g->pending)      /* pending BeginGetEvent: satisfy immediately */
    {
        async_result *r = g->pending;
        IMFAsyncCallback *cb = g->pending_cb;
        g->pending = NULL; g->pending_cb = NULL;
        r->event = e; IMFMediaEvent_AddRef(e);
        free(n);
        LeaveCriticalSection(&g->cs);
        eg_complete_pending(g, r, cb);
        return S_OK;
    }
    if (g->tail) g->tail->next = n; else g->head = n;
    g->tail = n;
    LeaveCriticalSection(&g->cs);
    return S_OK;
}

static HRESULT eg_getevent(event_gen *g, DWORD flags, IMFMediaEvent **out)
{
    HRESULT hr = MF_E_NO_EVENTS_AVAILABLE;
    (void)flags;
    EnterCriticalSection(&g->cs);
    if (g->shutdown) hr = MF_E_SHUTDOWN;
    else if (g->head) {
        ev_node *n = g->head;
        *out = n->event;   /* ref transferred */
        g->head = n->next;
        if (!g->head) g->tail = NULL;
        free(n);
        hr = S_OK;
    }
    LeaveCriticalSection(&g->cs);
    return hr;
}

static HRESULT eg_begin(event_gen *g, IMFAsyncCallback *cb, IUnknown *state)
{
    EnterCriticalSection(&g->cs);
    if (g->shutdown) { LeaveCriticalSection(&g->cs); return MF_E_SHUTDOWN; }
    if (g->pending)  { LeaveCriticalSection(&g->cs); return MF_E_MULTIPLE_SUBSCRIBERS; }
    async_result *r = async_result_new(state, NULL, S_OK);
    if (g->head) {
        ev_node *n = g->head;
        r->event = n->event; IMFMediaEvent_AddRef(r->event);
        g->head = n->next; if (!g->head) g->tail = NULL;
        free(n);
        IMFAsyncCallback_AddRef(cb);
        LeaveCriticalSection(&g->cs);
        eg_complete_pending(g, r, cb);
        return S_OK;
    }
    g->pending = r;
    g->pending_cb = cb; IMFAsyncCallback_AddRef(cb);
    LeaveCriticalSection(&g->cs);
    return S_OK;
}

static HRESULT eg_end(event_gen *g, IMFAsyncResult *res, IMFMediaEvent **out)
{
    (void)g;
    async_result *r = CONTAINING_RECORD(res, async_result, IMFAsyncResult_iface);
    if (!r->event) return FAILED(r->status) ? r->status : MF_E_NO_EVENTS_AVAILABLE;
    *out = r->event; IMFMediaEvent_AddRef(*out);
    return S_OK;
}

static void eg_shutdown(event_gen *g)
{
    async_result *r; IMFAsyncCallback *cb;
    EnterCriticalSection(&g->cs);
    g->shutdown = 1;
    while (g->head) { ev_node *n = g->head; g->head = n->next; IMFMediaEvent_Release(n->event); free(n); }
    g->tail = NULL;
    r = g->pending; cb = g->pending_cb;
    g->pending = NULL; g->pending_cb = NULL;
    LeaveCriticalSection(&g->cs);
    if (r) { r->status = MF_E_SHUTDOWN; eg_complete_pending(g, r, cb); }
}

/* ------------------------------------------------------------------ */
/* Wildcard IMFMediaType                                               */
/* ------------------------------------------------------------------ */

struct audiosrc;
struct typehandler;

typedef struct wild_type {
    IMFMediaType IMFMediaType_iface;
    LONG refcount;
    IMFMediaType *inner;
} wild_type;

static void wild_free(wild_type *t) { IMFMediaType_Release(t->inner); free(t); }
static HRESULT WINAPI wild_QI(IMFMediaType *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFMediaType) || IsEqualIID(riid, &IID_IMFAudioMediaType) ||
        IsEqualIID(riid, &IID_IMFAttributes) || IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFMediaType_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(wild, wild_type, IMFMediaType_iface, wild_free)
DEF_ATTR_DELEGATES(wild, wild_type, IMFMediaType_iface, inner)

static HRESULT WINAPI wild_GetMajorType(IMFMediaType *i, GUID *g)
{ wild_type *w = CONTAINING_RECORD(i, wild_type, IMFMediaType_iface); return IMFMediaType_GetMajorType(w->inner, g); }
static HRESULT WINAPI wild_IsCompressedFormat(IMFMediaType *i, BOOL *c)
{ wild_type *w = CONTAINING_RECORD(i, wild_type, IMFMediaType_iface); *c = FALSE; (void)w; return S_OK; }

/* Accept ANY audio type as "equal": capture the requester's attributes
 * into this object so the source can learn the requested format. */
static HRESULT WINAPI wild_IsEqual(IMFMediaType *i, IMFMediaType *other, DWORD *flags)
{
    wild_type *w = CONTAINING_RECORD(i, wild_type, IMFMediaType_iface);
    GUID major = GUID_NULL;
    *flags = 0;
    if (!other) return E_INVALIDARG;
    if (SUCCEEDED(IMFMediaType_GetMajorType(other, &major)) &&
        IsEqualGUID(&major, &MFMediaType_Audio))
    {
        IMFAttributes_DeleteAllItems((IMFAttributes *)w->inner);
        IMFAttributes_CopyAllItems((IMFAttributes *)other, (IMFAttributes *)w->inner);
        *flags = MF_MEDIATYPE_EQUAL_MAJOR_TYPES | MF_MEDIATYPE_EQUAL_FORMAT_TYPES |
                 MF_MEDIATYPE_EQUAL_FORMAT_DATA | MF_MEDIATYPE_EQUAL_FORMAT_USER_DATA;
    }
    return S_OK;
}
static HRESULT WINAPI wild_GetRepresentation(IMFMediaType *i, GUID riid, void **v)
{ wild_type *w = CONTAINING_RECORD(i, wild_type, IMFMediaType_iface); return IMFMediaType_GetRepresentation(w->inner, riid, v); }
static HRESULT WINAPI wild_FreeRepresentation(IMFMediaType *i, GUID riid, void *v)
{ wild_type *w = CONTAINING_RECORD(i, wild_type, IMFMediaType_iface); return IMFMediaType_FreeRepresentation(w->inner, riid, v); }

static const IMFMediaTypeVtbl wild_vtbl = {
    (void*)wild_QI, (void*)wild_AddRef, (void*)wild_Release,
    ATTR_VTABLE_ENTRIES(wild),
    wild_GetMajorType, wild_IsCompressedFormat, wild_IsEqual,
    wild_GetRepresentation, wild_FreeRepresentation,
};

static wild_type *wild_type_new(DWORD rate, DWORD ch)
{
    wild_type *t = calloc(1, sizeof(*t));
    IMFMediaType *inner;
    t->IMFMediaType_iface.lpVtbl = &wild_vtbl;
    t->refcount = 1;
    if (FAILED(MFCreateMediaType(&inner))) { free(t); return NULL; }
    t->inner = inner;
    IMFMediaType_SetGUID(inner, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(inner, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
    IMFMediaType_SetUINT32(inner, &MF_MT_AUDIO_NUM_CHANNELS, ch);
    IMFMediaType_SetUINT32(inner, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(inner, &MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    IMFMediaType_SetUINT32(inner, &MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 4);
    IMFMediaType_SetUINT32(inner, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * ch * 4);
    IMFMediaType_SetUINT32(inner, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    return t;
}

/* ------------------------------------------------------------------ */
/* Custom IMFMediaTypeHandler                                          */
/* ------------------------------------------------------------------ */

typedef struct typehandler {
    IMFMediaTypeHandler IMFMediaTypeHandler_iface;
    LONG refcount;
    struct audiosrc *src;          /* weak back-pointer */
    wild_type *wildcard;
    IMFMediaType *current;
} typehandler;

static void th_free(typehandler *h)
{
    IMFMediaType_Release(&h->wildcard->IMFMediaType_iface);
    if (h->current) IMFMediaType_Release(h->current);
    free(h);
}
static HRESULT WINAPI th_QI(IMFMediaTypeHandler *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFMediaTypeHandler) || IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFMediaTypeHandler_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(th, typehandler, IMFMediaTypeHandler_iface, th_free)

static HRESULT WINAPI th_IsMediaTypeSupported(IMFMediaTypeHandler *i, IMFMediaType *t, IMFMediaType **best)
{
    GUID major = GUID_NULL;
    if (best) *best = NULL;
    if (SUCCEEDED(IMFMediaType_GetMajorType(t, &major)) && IsEqualGUID(&major, &MFMediaType_Audio))
        return S_OK;
    return MF_E_INVALIDMEDIATYPE;
}
static HRESULT WINAPI th_GetMediaTypeCount(IMFMediaTypeHandler *i, DWORD *c) { *c = 1; return S_OK; }
static HRESULT WINAPI th_GetMediaTypeByIndex(IMFMediaTypeHandler *i, DWORD idx, IMFMediaType **t)
{
    typehandler *h = CONTAINING_RECORD(i, typehandler, IMFMediaTypeHandler_iface);
    if (idx > 0) return MF_E_NO_MORE_TYPES;
    *t = &h->wildcard->IMFMediaType_iface;
    IMFMediaType_AddRef(*t);
    return S_OK;
}
extern void shim_on_media_type_set(struct audiosrc *src, IMFMediaType *type);
static HRESULT WINAPI th_SetCurrentMediaType(IMFMediaTypeHandler *i, IMFMediaType *t)
{
    typehandler *h = CONTAINING_RECORD(i, typehandler, IMFMediaTypeHandler_iface);
    GUID major = GUID_NULL;
    if (FAILED(IMFMediaType_GetMajorType(t, &major)) || !IsEqualGUID(&major, &MFMediaType_Audio))
        return MF_E_INVALIDMEDIATYPE;
    if (h->current) IMFMediaType_Release(h->current);
    h->current = t; IMFMediaType_AddRef(t);
    if (h->src) shim_on_media_type_set(h->src, t);
    return S_OK;
}
static HRESULT WINAPI th_GetCurrentMediaType(IMFMediaTypeHandler *i, IMFMediaType **t)
{
    typehandler *h = CONTAINING_RECORD(i, typehandler, IMFMediaTypeHandler_iface);
    *t = h->current; IMFMediaType_AddRef(*t); return S_OK;
}
static HRESULT WINAPI th_GetMajorType(IMFMediaTypeHandler *i, GUID *g)
{ *g = MFMediaType_Audio; return S_OK; }

static const IMFMediaTypeHandlerVtbl th_vtbl = {
    (void*)th_QI, (void*)th_AddRef, (void*)th_Release,
    th_IsMediaTypeSupported, th_GetMediaTypeCount, th_GetMediaTypeByIndex,
    th_SetCurrentMediaType, th_GetCurrentMediaType, th_GetMajorType,
};

/* ------------------------------------------------------------------ */
/* IMFStreamDescriptor                                                 */
/* ------------------------------------------------------------------ */

typedef struct streamdesc {
    IMFStreamDescriptor IMFStreamDescriptor_iface;
    LONG refcount;
    IMFAttributes *attrs;
    DWORD id;
    typehandler *handler;
} streamdesc;

static void sd_free(streamdesc *s)
{
    IMFAttributes_Release(s->attrs);
    IMFMediaTypeHandler_Release(&s->handler->IMFMediaTypeHandler_iface);
    free(s);
}
static HRESULT WINAPI sd_QI(IMFStreamDescriptor *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFStreamDescriptor) || IsEqualIID(riid, &IID_IMFAttributes) ||
        IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFStreamDescriptor_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(sd, streamdesc, IMFStreamDescriptor_iface, sd_free)
DEF_ATTR_DELEGATES(sd, streamdesc, IMFStreamDescriptor_iface, attrs)
static HRESULT WINAPI sd_GetStreamIdentifier(IMFStreamDescriptor *i, DWORD *id)
{ streamdesc *s = CONTAINING_RECORD(i, streamdesc, IMFStreamDescriptor_iface); *id = s->id; return S_OK; }
static HRESULT WINAPI sd_GetMediaTypeHandler(IMFStreamDescriptor *i, IMFMediaTypeHandler **h)
{ streamdesc *s = CONTAINING_RECORD(i, streamdesc, IMFStreamDescriptor_iface); *h = &s->handler->IMFMediaTypeHandler_iface; IMFMediaTypeHandler_AddRef(*h); return S_OK; }
static const IMFStreamDescriptorVtbl sd_vtbl = {
    (void*)sd_QI, (void*)sd_AddRef, (void*)sd_Release,
    ATTR_VTABLE_ENTRIES(sd),
    sd_GetStreamIdentifier, sd_GetMediaTypeHandler,
};

/* ------------------------------------------------------------------ */
/* Audio capture source (IMFMediaSource + IMFMediaStream)              */
/* ------------------------------------------------------------------ */

#define FIFO_SECONDS 4

typedef struct audiostream {
    IMFMediaStream IMFMediaStream_iface;
    LONG refcount;
    struct audiosrc *src;
    streamdesc *sd;
    event_gen eg;
    int stopped;
} audiostream;

typedef struct audiosrc {
    IMFMediaSource IMFMediaSource_iface;
    LONG refcount;
    CRITICAL_SECTION cs;
    event_gen eg;
    int shutdown;
    int started;
    int streaming;             /* between Start and Stop */
    WCHAR *endpoint_id;        /* may be NULL */
    IMFPresentationDescriptor *pd;
    streamdesc *sd;
    audiostream *stream;
    typehandler *handler;
    /* capture state */
    IAudioClient *client;
    IAudioCaptureClient *cap;
    WAVEFORMATEX *mixfmt;
    HANDLE thread, stop_evt, data_evt;
    int cap_kill;
    float *fifo;
    DWORD fifo_frames, fifo_r, fifo_w;
    CRITICAL_SECTION fifo_cs;
    int in_is_float;             /* 1 = float32, 0 = integer PCM */
    DWORD in_bits;
    /* requested output format */
    int have_req;
    GUID req_subtype;
    DWORD req_rate, req_ch, req_bits;
    LONGLONG next_ts;          /* 100ns units */
} audiosrc;

static void src_push_silence_float(audiosrc *s, DWORD frames);

/* ---- capture ---- */

static DWORD fifo_avail(audiosrc *s)
{
    return (s->fifo_w - s->fifo_r + s->fifo_frames) % s->fifo_frames;
}
static DWORD fifo_space(audiosrc *s)
{
    return s->fifo_frames - 1 - fifo_avail(s);
}
static void fifo_push(audiosrc *s, const float *p, DWORD frames, DWORD inch)
{
    DWORD i;
    EnterCriticalSection(&s->fifo_cs);
    for (i = 0; i < frames * inch; i++) {
        if (fifo_space(s) == 0) s->fifo_r = (s->fifo_r + 1) % s->fifo_frames;
        s->fifo[s->fifo_w] = p[i];
        s->fifo_w = (s->fifo_w + 1) % s->fifo_frames;
    }
    LeaveCriticalSection(&s->fifo_cs);
    SetEvent(s->data_evt);
}
static DWORD fifo_pop(audiosrc *s, float *out, DWORD frames, DWORD inch)
{
    DWORD i, n = frames * inch, got = 0;
    EnterCriticalSection(&s->fifo_cs);
    while (got < n && s->fifo_r != s->fifo_w) {
        out[got++] = s->fifo[s->fifo_r];
        s->fifo_r = (s->fifo_r + 1) % s->fifo_frames;
    }
    LeaveCriticalSection(&s->fifo_cs);
    return got / inch;
}

static DWORD WINAPI capture_thread(void *arg)
{
    audiosrc *s = arg;
    DWORD inch = s->mixfmt->nChannels;
    UINT64 pushed = 0, iter = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (!s->cap_kill)
    {
        WaitForSingleObject(s->stop_evt, 10);
        if (s->cap_kill) break;
        if (!s->client || !s->streaming) continue;
        {
            UINT32 packet = 0;
            HRESULT ghr = IAudioCaptureClient_GetNextPacketSize(s->cap, &packet);
            if (++iter % 100 == 0)
                LOG("capture thread: iter=%llu ghr=%#lx pushed=%llu frames streaming=%d\n",
                    (unsigned long long)iter, ghr, (unsigned long long)pushed, s->streaming);
            if (FAILED(ghr)) continue;
            while (packet && !s->cap_kill)
            {
                BYTE *data = NULL; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(IAudioCaptureClient_GetBuffer(s->cap, &data, &frames, &flags, NULL, NULL)))
                    break;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data)
                {
                    /* convert source format (float32 or int16/24/32) to float fifo */
                    DWORD n = frames * inch, k;
                    float *conv = malloc(n * sizeof(float));
                    if (s->in_is_float)
                        memcpy(conv, data, n * sizeof(float));
                    else if (s->in_bits == 16) {
                        const short *p = (const short *)data;
                        for (k = 0; k < n; k++) conv[k] = p[k] / 32768.0f;
                    } else if (s->in_bits == 24) {
                        const BYTE *p = data;
                        for (k = 0; k < n; k++) {
                            long v = p[3*k] | (p[3*k+1] << 8) | (p[3*k+2] << 16);
                            if (v & 0x800000) v -= 0x1000000;
                            conv[k] = v / 8388608.0f;
                        }
                    } else {
                        const int *p = (const int *)data;
                        for (k = 0; k < n; k++) conv[k] = (float)(p[k] / 2147483648.0);
                    }
                    fifo_push(s, conv, frames, inch);
                    pushed += frames;
                    free(conv);
                }
                else
                    src_push_silence_float(s, frames);
                IAudioCaptureClient_ReleaseBuffer(s->cap, frames);
                IAudioCaptureClient_GetNextPacketSize(s->cap, &packet);
            }
        }
    }
    CoUninitialize();
    return 0;
}

static void src_push_silence_float(audiosrc *s, DWORD frames)
{
    float z[1024] = {0};
    DWORD inch = s->mixfmt->nChannels;
    DWORD per = 1024 / inch;
    while (frames) { DWORD n = frames < per ? frames : per; fifo_push(s, z, n, inch); frames -= n; }
}

static HRESULT capture_open(audiosrc *s)
{
    HRESULT hr;
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;

    if (s->client) return S_OK;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&en);
    if (FAILED(hr)) { LOG("MMDeviceEnumerator failed %#lx\n", hr); return hr; }

    if (s->endpoint_id)
        hr = IMMDeviceEnumerator_GetDevice(en, s->endpoint_id, &dev);
    else
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eCapture, eConsole, &dev);
    IMMDeviceEnumerator_Release(en);
    if (FAILED(hr)) { LOG("GetDevice failed %#lx\n", hr); return hr; }

    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&s->client);
    IMMDevice_Release(dev);
    if (FAILED(hr)) { LOG("Activate IAudioClient %#lx\n", hr); return hr; }

    hr = IAudioClient_GetMixFormat(s->client, &s->mixfmt);
    if (FAILED(hr)) return hr;
    {
        WORD tag = s->mixfmt->wFormatTag;
        s->in_bits = s->mixfmt->wBitsPerSample;
        s->in_is_float = 0;
        if (tag == WAVE_FORMAT_EXTENSIBLE) {
            static const GUID SUB_PCM   = {0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
            static const GUID SUB_FLOAT = {0x00000003,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
            const WAVEFORMATEXTENSIBLE *x = (const WAVEFORMATEXTENSIBLE *)s->mixfmt;
            if (IsEqualGUID(&x->SubFormat, &SUB_FLOAT)) s->in_is_float = 1;
            else if (!IsEqualGUID(&x->SubFormat, &SUB_PCM)) { LOG("unknown mix subtype\n"); return E_FAIL; }
        } else if (tag == WAVE_FORMAT_IEEE_FLOAT || tag == 3) s->in_is_float = 1;
        LOG("capture mix format: tag=%u rate=%lu ch=%lu bits=%u float=%d\n",
            s->mixfmt->wFormatTag, s->mixfmt->nSamplesPerSec, s->mixfmt->nChannels,
            s->mixfmt->wBitsPerSample, s->in_is_float);
    }

    hr = IAudioClient_Initialize(s->client, AUDCLNT_SHAREMODE_SHARED, 0, 500000, 0, s->mixfmt, NULL);
    if (FAILED(hr)) { LOG("capture Initialize %#lx\n", hr); return hr; }

    hr = IAudioClient_GetService(s->client, &IID_IAudioCaptureClient, (void **)&s->cap);
    if (FAILED(hr)) { LOG("capture GetService %#lx\n", hr); return hr; }

    s->fifo_frames = s->mixfmt->nSamplesPerSec * s->mixfmt->nChannels * FIFO_SECONDS;
    s->fifo = calloc(s->fifo_frames, sizeof(float));
    s->fifo_r = s->fifo_w = 0;
    s->stop_evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->data_evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&s->fifo_cs);
    s->cap_kill = 0;
    s->thread = CreateThread(NULL, 0, capture_thread, s, 0, NULL);
    return S_OK;
}

static void capture_close(audiosrc *s)
{
    if (s->thread) {
        s->cap_kill = 1;
        SetEvent(s->stop_evt);   /* also wakes wait loop via timeout anyway */
        WaitForSingleObject(s->thread, 3000);
        CloseHandle(s->thread); s->thread = NULL;
    }
    if (s->cap) { IAudioCaptureClient_Release(s->cap); s->cap = NULL; }
    if (s->client) { IAudioClient_Release(s->client); s->client = NULL; }
    if (s->mixfmt) { CoTaskMemFree(s->mixfmt); s->mixfmt = NULL; }
    if (s->fifo) { free(s->fifo); s->fifo = NULL; }
    if (s->stop_evt) { CloseHandle(s->stop_evt); s->stop_evt = NULL; }
    if (s->data_evt) { CloseHandle(s->data_evt); s->data_evt = NULL; }
}

/* ---- format conversion + sample production ---- */

void shim_on_media_type_set(struct audiosrc *src, IMFMediaType *t)
{
    IMFAttributes *a = (IMFAttributes *)t;
    UINT32 v;
    src->have_req = 1;
    src->req_subtype = MFAudioFormat_PCM;
    IMFAttributes_GetGUID(a, &MF_MT_SUBTYPE, &src->req_subtype);
    src->req_rate = 48000; src->req_ch = 2; src->req_bits = 16;
    if (SUCCEEDED(IMFAttributes_GetUINT32(a, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &v))) src->req_rate = v;
    if (SUCCEEDED(IMFAttributes_GetUINT32(a, &MF_MT_AUDIO_NUM_CHANNELS, &v))) src->req_ch = v;
    if (SUCCEEDED(IMFAttributes_GetUINT32(a, &MF_MT_AUDIO_BITS_PER_SAMPLE, &v))) src->req_bits = v;
    LOG("requested format: rate=%lu ch=%lu bits=%lu\n", src->req_rate, src->req_ch, src->req_bits);
}

static IMFMediaStream *stream_iface(audiostream *s) { return &s->IMFMediaStream_iface; }

/* Build one media sample from captured float32 data converted to the
 * requested format.  Called by RequestSample. */
static HRESULT produce_sample(audiosrc *s, IMFSample **out)
{
    DWORD inch, in_rate, out_rate, out_ch, bits;
    int is_float;
    DWORD in_have, in_take, out_frames;
    float *inbuf;
    BYTE *payload;
    DWORD payload_size;
    IMFSample *sample = NULL;
    IMFMediaBuffer *mbuf = NULL;
    HRESULT hr;

    *out = NULL;
    if (!s->mixfmt) return MF_E_NOT_INITIALIZED;
    inch = s->mixfmt->nChannels;
    in_rate = s->mixfmt->nSamplesPerSec;
    out_rate = s->have_req ? s->req_rate : in_rate;
    out_ch   = s->have_req ? s->req_ch   : inch;
    bits     = s->have_req ? s->req_bits : 32;
    is_float = s->have_req ? IsEqualGUID(&s->req_subtype, &MFAudioFormat_Float) : TRUE;

    /* Emit fixed 20ms output chunks: vocoders/audio encoders expect a
     * steady cadence, and irregular chunk sizes make decoded audio choppy. */
    out_frames = out_rate / 50;
    if (!out_frames) out_frames = 1;
    in_take = (DWORD)(((UINT64)out_frames * in_rate) / out_rate);
    if (!in_take) in_take = 1;

    /* wait for the chunk's worth of input (bounded, aborts on stop) —
     * never emit padded silence mid-stream, that is what makes TX choppy */
    {
        DWORD deadline = GetTickCount() + 1500;
        while (fifo_avail(s) / inch < in_take) {
            if (s->shutdown || s->cap_kill || s->stream->stopped) return MF_E_MEDIA_SOURCE_WRONGSTATE;
            if (GetTickCount() > deadline) break;
            WaitForSingleObject(s->data_evt, 10);
        }
    }
    in_have = fifo_avail(s) / inch;

    inbuf = calloc(in_take * inch, sizeof(float));
    if (in_have) {
        DWORD got = fifo_pop(s, inbuf, in_take, inch);
        (void)got; /* remaining tail stays zeroed */
    }

    {
        static int dbg;
        if (++dbg % 20 == 1) {
            double pk = 0; DWORD k;
            for (k = 0; k < in_take * inch; k++) { double a = inbuf[k] < 0 ? -inbuf[k] : inbuf[k]; if (a > pk) pk = a; }
            LOG("produce_sample: in_have=%lu in_take=%lu out=%lu peak_in=%.4f\n", in_have, in_take, out_frames, pk);
        }
    }

    {
        DWORD bps = is_float ? 4 : bits / 8;
        payload_size = out_frames * out_ch * bps;
        payload = calloc(payload_size, 1);

        DWORD oi;
        if (in_rate % out_rate == 0)
        {
            /* Integer-ratio decimation: boxcar-average R input frames per
             * output sample (a simple anti-alias lowpass).  Linear
             * interpolation here aliases badly (6:1 for 48k->8k) and
             * produces harsh, warbly TX audio. */
            DWORD R = in_rate / out_rate, c, r;
            for (oi = 0; oi < out_frames; oi++) {
                for (c = 0; c < out_ch; c++) {
                    DWORD src_ch = c < inch ? c : 0;
                    double v = 0.0;
                    for (r = 0; r < R; r++) {
                        DWORD idx = oi * R + r;
                        if (idx >= in_take) idx = in_take - 1;
                        v += inbuf[idx * inch + src_ch];
                        if (out_ch == 1 && inch == 2)
                            v += inbuf[idx * inch + 1];
                    }
                    v /= (double)R;
                    if (out_ch == 1 && inch == 2) v /= 2.0;
                    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
                    if (is_float)        ((float *)payload)[oi * out_ch + c] = (float)v;
                    else if (bits == 16) ((short *)payload)[oi * out_ch + c] = (short)(v * 32767);
                    else if (bits == 24) { long x = (long)(v * 8388607); BYTE *p = payload + (oi * out_ch + c) * 3; p[0]=x&0xff; p[1]=(x>>8)&0xff; p[2]=(x>>16)&0xff; }
                    else                 ((int *)payload)[oi * out_ch + c] = (int)(v * 2147483647.0);
                }
            }
        }
        else
        {
            for (oi = 0; oi < out_frames; oi++) {
                double pos = (double)oi * in_rate / out_rate;
                DWORD i0 = (DWORD)pos;
                DWORD i1 = i0 + 1 < in_take ? i0 + 1 : i0;
                double frac = pos - i0;
                DWORD c;
                for (c = 0; c < out_ch; c++) {
                    DWORD src_ch = c < inch ? c : 0;
                    float a = inbuf[i0 * inch + src_ch], b = inbuf[i1 * inch + src_ch];
                    double v = a + (b - a) * frac;
                    if (out_ch == 1 && inch == 2) {
                        float a2 = inbuf[i0 * inch + 1], b2 = inbuf[i1 * inch + 1];
                        v = (v + a2 + (b2 - a2) * frac) / 2.0;
                    }
                    if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
                    if (is_float)            ((float *)payload)[oi * out_ch + c] = (float)v;
                    else if (bits == 16)     ((short *)payload)[oi * out_ch + c] = (short)(v * 32767);
                    else if (bits == 24) { long x = (long)(v * 8388607); BYTE *p = payload + (oi * out_ch + c) * 3; p[0]=x&0xff; p[1]=(x>>8)&0xff; p[2]=(x>>16)&0xff; }
                    else                    ((int *)payload)[oi * out_ch + c] = (int)(v * 2147483647.0);
                }
            }
        }
        free(inbuf);
    }

    hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = MFCreateMemoryBuffer(payload_size, &mbuf);
    if (SUCCEEDED(hr)) {
        BYTE *dst = NULL; DWORD maxlen = 0, curlen = 0;
        IMFMediaBuffer_Lock(mbuf, &dst, &maxlen, &curlen);
        memcpy(dst, payload, payload_size);
        IMFMediaBuffer_Unlock(mbuf);
        IMFMediaBuffer_SetCurrentLength(mbuf, payload_size);
        IMFSample_AddBuffer(sample, mbuf);
        IMFSample_SetSampleTime(sample, s->next_ts);
        IMFSample_SetSampleDuration(sample, (LONGLONG)out_frames * 10000000 / out_rate);
        s->next_ts += (LONGLONG)out_frames * 10000000 / out_rate;
        *out = sample;
    }
    free(payload);
    if (mbuf) IMFMediaBuffer_Release(mbuf);
    return SUCCEEDED(hr) ? S_OK : hr;
}

/* ---- IMFMediaStream ---- */

static void stream_free(audiostream *st)
{
    eg_destroy(&st->eg);
    if (st->sd) IMFStreamDescriptor_Release(&st->sd->IMFStreamDescriptor_iface);
    free(st);
}
static HRESULT WINAPI strm_QI(IMFMediaStream *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFMediaStream) || IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFMediaStream_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(strm, audiostream, IMFMediaStream_iface, stream_free)

static HRESULT WINAPI strm_GetEvent(IMFMediaEventGenerator *i, DWORD f, IMFMediaEvent **e)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); return eg_getevent(&s->eg, f, e); }
static HRESULT WINAPI strm_BeginGetEvent(IMFMediaEventGenerator *i, IMFAsyncCallback *cb, IUnknown *st)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); return eg_begin(&s->eg, cb, st); }
static HRESULT WINAPI strm_EndGetEvent(IMFMediaEventGenerator *i, IMFAsyncResult *r, IMFMediaEvent **e)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); return eg_end(&s->eg, r, e); }
static HRESULT WINAPI strm_QueueEvent(IMFMediaEventGenerator *i, MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT *v)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); return eg_queue(&s->eg, t, hr, v); }

static HRESULT WINAPI strm_GetMediaSource(IMFMediaStream *i, IMFMediaSource **ms)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); *ms = &s->src->IMFMediaSource_iface; IMFMediaSource_AddRef(*ms); return S_OK; }
static HRESULT WINAPI strm_GetStreamDescriptor(IMFMediaStream *i, IMFStreamDescriptor **sd)
{ audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface); *sd = &s->sd->IMFStreamDescriptor_iface; IMFStreamDescriptor_AddRef(*sd); return S_OK; }

static HRESULT WINAPI strm_RequestSample(IMFMediaStream *i, IUnknown *token)
{
    audiostream *s = CONTAINING_RECORD(i, audiostream, IMFMediaStream_iface);
    IMFSample *sample = NULL;
    PROPVARIANT v;
    HRESULT hr;
    (void)token;

    if (s->src->shutdown) return MF_E_SHUTDOWN;
    if (s->stopped) return MF_E_MEDIA_SOURCE_WRONGSTATE;
    hr = produce_sample(s->src, &sample);
    if (FAILED(hr)) return hr;
    PropVariantInit(&v);
    v.vt = VT_UNKNOWN;
    v.punkVal = (IUnknown *)sample;
    hr = eg_queue(&s->eg, MEMediaSample, S_OK, &v);
    IMFSample_Release(sample);
    return hr;
}

static const IMFMediaStreamVtbl strm_vtbl = {
    (void*)strm_QI, (void*)strm_AddRef, (void*)strm_Release,
    (void*)strm_GetEvent, (void*)strm_BeginGetEvent,
    (void*)strm_EndGetEvent, (void*)strm_QueueEvent,
    strm_GetMediaSource, strm_GetStreamDescriptor, strm_RequestSample,
};

/* ---- IMFMediaSource ---- */

static void src_free(audiosrc *s)
{
    capture_close(s);
    eg_destroy(&s->eg);
    if (s->endpoint_id) free(s->endpoint_id);
    if (s->pd) IMFPresentationDescriptor_Release(s->pd);
    if (s->stream) IMFMediaStream_Release(&s->stream->IMFMediaStream_iface);
    if (s->sd) IMFStreamDescriptor_Release(&s->sd->IMFStreamDescriptor_iface);
    DeleteCriticalSection(&s->cs);
    free(s);
}
static HRESULT WINAPI src_QI(IMFMediaSource *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFMediaSource) || IsEqualIID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFMediaSource_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(src, audiosrc, IMFMediaSource_iface, src_free)

static HRESULT WINAPI src_GetEvent(IMFMediaEventGenerator *i, DWORD f, IMFMediaEvent **e)
{ audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface); return eg_getevent(&s->eg, f, e); }
static HRESULT WINAPI src_BeginGetEvent(IMFMediaEventGenerator *i, IMFAsyncCallback *cb, IUnknown *st)
{ audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface); return eg_begin(&s->eg, cb, st); }
static HRESULT WINAPI src_EndGetEvent(IMFMediaEventGenerator *i, IMFAsyncResult *r, IMFMediaEvent **e)
{ audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface); return eg_end(&s->eg, r, e); }
static HRESULT WINAPI src_QueueEvent(IMFMediaEventGenerator *i, MediaEventType t, REFGUID g, HRESULT hr, const PROPVARIANT *v)
{ audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface); return eg_queue(&s->eg, t, hr, v); }

static HRESULT WINAPI src_GetCharacteristics(IMFMediaSource *i, DWORD *flags)
{ *flags = MFMEDIASOURCE_IS_LIVE; return S_OK; }

static HRESULT WINAPI src_CreatePresentationDescriptor(IMFMediaSource *i, IMFPresentationDescriptor **out)
{
    audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface);
    *out = s->pd; IMFPresentationDescriptor_AddRef(*out); return S_OK;
}

static HRESULT WINAPI src_Start(IMFMediaSource *i, IMFPresentationDescriptor *pd,
                                const GUID *tf, const PROPVARIANT *pos)
{
    audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface);
    PROPVARIANT v;
    HRESULT hr = S_OK;
    (void)pd; (void)tf; (void)pos;

    EnterCriticalSection(&s->cs);
    if (s->shutdown) { LeaveCriticalSection(&s->cs); return MF_E_SHUTDOWN; }
    if (s->streaming) { LeaveCriticalSection(&s->cs); return S_OK; }

    hr = capture_open(s);
    if (SUCCEEDED(hr)) {
        s->streaming = 1;
        s->stream->stopped = 0;
        if (s->client) IAudioClient_Start(s->client);
        if (!s->started) {
            s->started = 1;
            PropVariantInit(&v); v.vt = VT_UNKNOWN; v.punkVal = (IUnknown *)stream_iface(s->stream);
            eg_queue(&s->eg, MENewStream, S_OK, &v);
            PropVariantClear(&v);
        }
        PropVariantInit(&v); v.vt = VT_I8; v.hVal.QuadPart = s->next_ts;
        eg_queue(&s->stream->eg, MEStreamStarted, S_OK, &v);
        eg_queue(&s->eg, MESourceStarted, S_OK, &v);
        PropVariantClear(&v);
    }
    LeaveCriticalSection(&s->cs);
    LOG("source Start: %#lx\n", hr);
    return hr;
}

static HRESULT WINAPI src_Stop(IMFMediaSource *i)
{
    audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface);
    EnterCriticalSection(&s->cs);
    if (s->shutdown) { LeaveCriticalSection(&s->cs); return MF_E_SHUTDOWN; }
    if (s->streaming) {
        s->streaming = 0;
        s->stream->stopped = 1;
        if (s->client) IAudioClient_Stop(s->client);
        eg_queue(&s->stream->eg, MEStreamStopped, S_OK, NULL);
        eg_queue(&s->eg, MESourceStopped, S_OK, NULL);
    }
    LeaveCriticalSection(&s->cs);
    LOG("source Stop\n");
    return S_OK;
}

static HRESULT WINAPI src_Pause(IMFMediaSource *i)
{
    audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface);
    EnterCriticalSection(&s->cs);
    if (s->shutdown) { LeaveCriticalSection(&s->cs); return MF_E_SHUTDOWN; }
    if (s->streaming) {
        s->streaming = 0;
        if (s->client) IAudioClient_Stop(s->client);
        eg_queue(&s->eg, MESourcePaused, S_OK, NULL);
    }
    LeaveCriticalSection(&s->cs);
    LOG("source Pause\n");
    return S_OK;
}

static HRESULT WINAPI src_Shutdown(IMFMediaSource *i)
{
    audiosrc *s = CONTAINING_RECORD(i, audiosrc, IMFMediaSource_iface);
    EnterCriticalSection(&s->cs);
    if (!s->shutdown) {
        s->shutdown = 1;
        s->streaming = 0;
        s->stream->stopped = 1;
        capture_close(s);
        eg_shutdown(&s->stream->eg);
        eg_shutdown(&s->eg);
    }
    LeaveCriticalSection(&s->cs);
    LOG("source Shutdown\n");
    return S_OK;
}

static const IMFMediaSourceVtbl src_vtbl = {
    (void*)src_QI, (void*)src_AddRef, (void*)src_Release,
    (void*)src_GetEvent, (void*)src_BeginGetEvent,
    (void*)src_EndGetEvent, (void*)src_QueueEvent,
    src_GetCharacteristics, src_CreatePresentationDescriptor,
    src_Start, src_Stop, src_Pause, src_Shutdown,
};

static HRESULT create_audio_source(IMFAttributes *attrs, IMFMediaSource **out)
{
    audiosrc *s;
    WCHAR *ep = NULL;
    HRESULT hr;

    *out = NULL;

    if (attrs) {
        UINT32 len = 0;
        if (SUCCEEDED(IMFAttributes_GetStringLength(attrs,
                &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &len))) {
            ep = calloc(len + 1, sizeof(WCHAR));
            IMFAttributes_GetString(attrs,
                &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, ep, len + 1, &len);
        }
    }

    s = calloc(1, sizeof(*s));
    s->IMFMediaSource_iface.lpVtbl = &src_vtbl;
    s->refcount = 1;
    s->endpoint_id = ep;
    InitializeCriticalSection(&s->cs);
    eg_init(&s->eg);

    /* stream + descriptor + type handler */
    s->handler = calloc(1, sizeof(typehandler));
    s->handler->IMFMediaTypeHandler_iface.lpVtbl = &th_vtbl;
    s->handler->refcount = 1;
    s->handler->src = s;
    s->handler->wildcard = wild_type_new(48000, 2);
    s->handler->current = &s->handler->wildcard->IMFMediaType_iface;
    IMFMediaType_AddRef(s->handler->current);

    s->sd = calloc(1, sizeof(streamdesc));
    s->sd->IMFStreamDescriptor_iface.lpVtbl = &sd_vtbl;
    s->sd->refcount = 1;
    s->sd->id = 0;
    s->sd->handler = s->handler;
    IMFMediaTypeHandler_AddRef(&s->handler->IMFMediaTypeHandler_iface);
    MFCreateAttributes(&s->sd->attrs, 0);

    s->stream = calloc(1, sizeof(audiostream));
    s->stream->IMFMediaStream_iface.lpVtbl = &strm_vtbl;
    s->stream->refcount = 1;
    s->stream->src = s;
    s->stream->sd = s->sd;
    IMFStreamDescriptor_AddRef(&s->sd->IMFStreamDescriptor_iface);
    eg_init(&s->stream->eg);

    {
        IMFStreamDescriptor *sd = &s->sd->IMFStreamDescriptor_iface;
        hr = MFCreatePresentationDescriptor(1, &sd, &s->pd);
        if (SUCCEEDED(hr)) hr = IMFPresentationDescriptor_SelectStream(s->pd, 0);
    }
    if (FAILED(hr)) { LOG("presentation descriptor %#lx\n", hr); src_free(s); return hr; }

    *out = &s->IMFMediaSource_iface;
    LOG("audio capture source created (endpoint %ls)\n", ep ? ep : L"(default)");
    return S_OK;
}

/* ------------------------------------------------------------------ */
/* IMFActivate for enumerated devices                                  */
/* ------------------------------------------------------------------ */

typedef struct activate_obj {
    IMFActivate IMFActivate_iface;
    LONG refcount;
    IMFAttributes *attrs;
    WCHAR *endpoint_id;
    IUnknown *object;
} activate_obj;

static void act_free(activate_obj *a)
{
    if (a->object) IUnknown_Release(a->object);
    if (a->endpoint_id) free(a->endpoint_id);
    IMFAttributes_Release(a->attrs);
    free(a);
}
static HRESULT WINAPI act_QI(IMFActivate *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IMFActivate) || IsEqualIID(riid, &IID_IMFAttributes) ||
        IsEqualIID(riid, &IID_IUnknown))
    { *obj = iface; IMFActivate_AddRef(iface); return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
DEF_REFS(act, activate_obj, IMFActivate_iface, act_free)
DEF_ATTR_DELEGATES(act, activate_obj, IMFActivate_iface, attrs)

static HRESULT WINAPI act_ActivateObject(IMFActivate *i, REFIID riid, void **obj)
{
    activate_obj *a = CONTAINING_RECORD(i, activate_obj, IMFActivate_iface);
    if (!a->object) {
        IMFMediaSource *src = NULL;
        if (FAILED(create_audio_source(a->attrs, &src))) return E_FAIL;
        a->object = (IUnknown *)src;
    }
    return IUnknown_QueryInterface(a->object, riid, obj);
}
static HRESULT WINAPI act_ShutdownObject(IMFActivate *i)
{
    activate_obj *a = CONTAINING_RECORD(i, activate_obj, IMFActivate_iface);
    if (a->object) {
        IMFMediaSource *src;
        if (SUCCEEDED(IUnknown_QueryInterface(a->object, &IID_IMFMediaSource, (void **)&src))) {
            IMFMediaSource_Shutdown(src);
            IMFMediaSource_Release(src);
        }
        IUnknown_Release(a->object);
        a->object = NULL;
    }
    return S_OK;
}
static HRESULT WINAPI act_DetachObject(IMFActivate *i) { return E_NOTIMPL; }

static const IMFActivateVtbl act_vtbl = {
    (void*)act_QI, (void*)act_AddRef, (void*)act_Release,
    ATTR_VTABLE_ENTRIES(act),
    act_ActivateObject, act_ShutdownObject, act_DetachObject,
};

static IMFActivate *activate_new(const WCHAR *endpoint_id, const WCHAR *name)
{
    activate_obj *a = calloc(1, sizeof(*a));
    a->IMFActivate_iface.lpVtbl = &act_vtbl;
    a->refcount = 1;
    MFCreateAttributes(&a->attrs, 0);
    IMFAttributes_SetGUID(a->attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                          &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
    IMFAttributes_SetString(a->attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, endpoint_id);
    if (name) IMFAttributes_SetString(a->attrs, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name);
    a->endpoint_id = _wcsdup(endpoint_id);
    return &a->IMFActivate_iface;
}

/* ------------------------------------------------------------------ */
/* Exported MF entry points                                            */
/* ------------------------------------------------------------------ */

HRESULT WINAPI MFEnumDeviceSources(IMFAttributes *attrs, IMFActivate ***sources, UINT32 *count)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDeviceCollection *col = NULL;
    HRESULT hr;
    UINT32 i, n = 0;

    if (!attrs || !sources || !count) return E_INVALIDARG;
    *sources = NULL; *count = 0;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&en);
    if (FAILED(hr)) return hr;
    hr = IMMDeviceEnumerator_EnumAudioEndpoints(en, eCapture, DEVICE_STATE_ACTIVE, &col);
    if (FAILED(hr)) { IMMDeviceEnumerator_Release(en); return hr; }
    IMMDeviceCollection_GetCount(col, &n);

    *sources = CoTaskMemAlloc(n * sizeof(IMFActivate *));
    for (i = 0; i < n; i++) {
        IMMDevice *dev = NULL;
        LPWSTR id = NULL, name = NULL;
        IPropertyStore *ps = NULL;
        PROPVARIANT v;

        if (FAILED(IMMDeviceCollection_Item(col, i, &dev))) continue;
        IMMDevice_GetId(dev, &id);
        PropVariantInit(&v);
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps))) {
            if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                name = _wcsdup(v.pwszVal);
            PropVariantClear(&v);
            IPropertyStore_Release(ps);
        }
        (*sources)[i] = activate_new(id ? id : L"", name);
        if (id) CoTaskMemFree(id);
        free(name);
        IMMDevice_Release(dev);
    }
    IMMDeviceCollection_Release(col);
    IMMDeviceEnumerator_Release(en);
    *count = n;
    return S_OK;
}

HRESULT WINAPI MFCreateDeviceSource(IMFAttributes *attrs, IMFMediaSource **source)
{
    if (!attrs || !source) return E_INVALIDARG;
    return create_audio_source(attrs, source);
}

HRESULT WINAPI MFCreateDeviceSourceActivate(IMFActivate *activate, IMFMediaSource **source)
{
    if (!activate || !source) return E_INVALIDARG;
    return IMFActivate_ActivateObject(activate, &IID_IMFMediaSource, (void **)source);
}

/* COM server stubs — mf.dll is never used as an inproc server here. */
HRESULT WINAPI DllGetClassObject(REFCLSID c, REFIID i, void **o) { return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *res)
{
    (void)inst; (void)res;
    if (reason == DLL_PROCESS_ATTACH)
        LOG("mf.dll shim loaded (WASAPI-backed audio capture source)\n");
    return TRUE;
}
