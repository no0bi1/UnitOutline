// UnitOutline: reaction-colored silhouette outline on the target and mouseover unit.
// WoW 1.12.1 (build 5875), Direct3D 9.
//
// Selected units' M2 batches are re-issued flat-colored into an offscreen mask. When the world has
// finished rendering (before the UI), the mask is dilated into a ring and blended over the frame.

#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cmath>
#ifdef OUTLINE_DEBUG
#include <cstdio>
#endif

#include "MinHook.h"

namespace
{
    // ---- 1.12.1.5875 addresses
    constexpr uintptr_t kRenderWorld = 0x482D70;  // world frame render, thiscall
    constexpr uintptr_t kReleaseD3d  = 0x599900;  // runs before a device Reset, thiscall(this, int)
    constexpr uintptr_t kGxDevice    = 0xC0ED38;  // *kGxDevice is the CGxDevice, D3D device at +0x38A8
    constexpr uintptr_t kModelDraw   = 0x58A830;  // model geometry draw, only called from the batch routines (text isn't)
    constexpr uintptr_t kBatch[6]    = { 0x70CB30, 0x70CF70, 0x70D330, 0x70D820, 0x70D8B0, 0x70D960 };
    constexpr int       kDipVtable   = 82;        // IDirect3DDevice9::DrawIndexedPrimitive

    // Game functions, same conventions as UnitXP SP3.
    typedef uint64_t (__fastcall* UnitGuidFn)(const char*);
    typedef uint32_t (__fastcall* GetObjectFn)(uint64_t);
    typedef int      (__thiscall* ReactionFn)(uint32_t self, uint32_t target);
    typedef void*    (__fastcall* GetContextFn)(void);
    typedef double   (__fastcall* ToNumberFn)(void* L, int index);
    typedef int      (__fastcall* LuaCFunction)(void* L);
    struct LuaReg { const char* name; LuaCFunction func; };
    typedef void     (__fastcall* OpenLibFn)(void* L, const char* ns, const LuaReg* reg, int upvalues);

    const UnitGuidFn    GameUnitGuid   = reinterpret_cast<UnitGuidFn>(0x515970);
    const GetObjectFn   GameGetObject  = reinterpret_cast<GetObjectFn>(0x464870);
    const ReactionFn    GameReaction   = reinterpret_cast<ReactionFn>(0x6061E0);
    const GetContextFn  GameContext    = reinterpret_cast<GetContextFn>(0x7040D0);
    const ToNumberFn    GameToNumber   = reinterpret_cast<ToNumberFn>(0x6F3620);
    const OpenLibFn     GameOpenLib    = reinterpret_cast<OpenLibFn>(0x6F4DC0);

    typedef int  (__thiscall* RenderWorldFn)(void*);
    typedef void (__thiscall* ReleaseFn)(uint32_t, int);
    typedef void (__fastcall* ModelDrawFn)(void*, uint32_t);
    typedef int  (__thiscall* BatchFn)(void*);
    typedef int  (__thiscall* Batch2Fn)(void*, uint32_t, uint32_t);
    typedef HRESULT (STDMETHODCALLTYPE* DipFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);

    RenderWorldFn origRenderWorld;
    ReleaseFn     origRelease;
    ModelDrawFn   origModelDraw;
    uintptr_t     origBatch[6];
    DipFn         origDip;

    // ---- settings, pushed from the GUI addon
    struct Config
    {
        bool  enable = true, mouseover = true, target = true;
        float thickness = 2.0f, opacity = 1.0f;
        float hostile[3]  = { 1.0f, 0.1f, 0.1f };
        float neutral[3]  = { 1.0f, 1.0f, 0.0f };
        float friendly[3] = { 0.0f, 0.8f, 0.0f };
    } cfg;

    int __fastcall LuaConfig(void* L)
    {
        double v[14];
        for (int i = 0; i < 14; ++i) v[i] = GameToNumber(L, i + 1);
        cfg.enable    = v[0] != 0;
        cfg.mouseover = v[1] != 0;
        cfg.target    = v[2] != 0;
        cfg.thickness = static_cast<float>(v[3] < 0.5 ? 0.5 : v[3] > 6.0 ? 6.0 : v[3]);
        cfg.opacity   = static_cast<float>(v[4] < 0.0 ? 0.0 : v[4] > 1.0 ? 1.0 : v[4]);
        for (int i = 0; i < 3; ++i)
        {
            cfg.hostile[i]  = static_cast<float>(v[5 + i]);
            cfg.neutral[i]  = static_cast<float>(v[8 + i]);
            cfg.friendly[i] = static_cast<float>(v[11 + i]);
        }
        return 0;
    }

    const LuaReg luaReg[] = { { "Config", LuaConfig }, { nullptr, nullptr } };
    void* luaState;

    // ---- selected models
    struct Target { uint32_t model; float rgb[3]; };
    Target g_t[16];
    int    g_n;
    int    g_cur = -1;   // index into g_t while a selected model's batch is being drawn
    bool   g_draw;       // inside that batch's geometry draw (the batch routine also draws other things)

    bool Valid(uint32_t p) { return p != 0 && (p & 1) == 0; }

    void AddModel(uint32_t m, const float* rgb, int depth)
    {
        if (!Valid(m) || g_n >= 16) return;
        Target& t = g_t[g_n++];
        t.model = m;
        t.rgb[0] = rgb[0]; t.rgb[1] = rgb[1]; t.rgb[2] = rgb[2];
        // attachments (weapons, mount) hang off the model's child list: +0x1DC first, +0x1E4 next sibling
        if (depth < 3)
            for (uint32_t c = *reinterpret_cast<uint32_t*>(m + 0x1DC); Valid(c) && g_n < 16; c = *reinterpret_cast<uint32_t*>(c + 0x1E4))
                AddModel(c, rgb, depth + 1);
    }

    uint32_t UnitObject(const char* token)
    {
        const uint64_t guid = GameUnitGuid(token);
        const uint32_t obj = guid ? GameGetObject(guid) : 0;
        return Valid(obj) ? obj : 0;
    }

    void AddUnit(uint32_t obj, uint32_t player)
    {
        const int type = *reinterpret_cast<int*>(obj + 0x14);
        if (type != 3 && type != 4) return;                       // unit or player
        const int reaction = GameReaction(player, obj);            // 0 hated .. 6 revered
        const float* rgb = reaction <= 2 ? cfg.hostile : reaction == 3 ? cfg.neutral : cfg.friendly;
        AddModel(*reinterpret_cast<uint32_t*>(obj + 0xD8), rgb, 0);
    }

    void Select()
    {
        g_n = 0;
        if (!cfg.enable) return;
        const uint32_t player = UnitObject("player");
        if (!player) return;
        const uint32_t target = cfg.target ? UnitObject("target") : 0;
        const uint32_t over   = cfg.mouseover ? UnitObject("mouseover") : 0;
        if (target && target != player) AddUnit(target, player);
        if (over && over != player && over != target) AddUnit(over, player);
    }

    int Find(uint32_t model)
    {
        for (int i = 0; i < g_n; ++i)
            if (g_t[i].model == model) return i;
        return -1;
    }

    // ---- D3D resources
    IDirect3DTexture9*    maskTex;
    IDirect3DTexture9*    ringTex;
    IDirect3DSurface9*    maskSurf;
    IDirect3DSurface9*    ringSurf;
    IDirect3DStateBlock9* stateBlock;
    UINT g_w, g_h;
    bool g_stamped;       // mask holds this frame's silhouettes
#ifdef OUTLINE_DEBUG
    // per-second counters, written to UnitOutline.log next to WoW.exe
    struct Stats { unsigned frames, selected, batches, matched, draws, stamped, mt[6], dt[6], prim[6]; } st;
    int g_type;
    DWORD statTick;

    void Log(const char* text)
    {
        if (FILE* f = fopen("UnitOutline.log", "a")) { fputs(text, f); fclose(f); }
    }

    void FlushStats()
    {
        const DWORD now = GetTickCount();
        if (now - statTick < 1000) return;
        statTick = now;
        char line[256];
        uint32_t tm = 0;
        if (g_n) tm = g_t[0].model;
        sprintf(line, "frames=%u selected=%u matched=%u draws=%u model=%08X n=%d | type:matched/draws/prims 0:%u/%u/%u 1:%u/%u/%u 2:%u/%u/%u 3:%u/%u/%u 4:%u/%u/%u 5:%u/%u/%u\n",
                st.frames, st.selected, st.matched, st.draws, tm, g_n,
                st.mt[0], st.dt[0], st.prim[0], st.mt[1], st.dt[1], st.prim[1], st.mt[2], st.dt[2], st.prim[2],
                st.mt[3], st.dt[3], st.prim[3], st.mt[4], st.dt[4], st.prim[4], st.mt[5], st.dt[5], st.prim[5]);
        Log(line);
        st = Stats{};
    }
#endif

    template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

    void ReleaseResources()
    {
        Release(maskSurf); Release(ringSurf); Release(maskTex); Release(ringTex); Release(stateBlock);
        g_stamped = false;
    }

    IDirect3DDevice9* Device()
    {
        const uint32_t gx = *reinterpret_cast<uint32_t*>(kGxDevice);
        return Valid(gx) ? *reinterpret_cast<IDirect3DDevice9**>(gx + 0x38A8) : nullptr;
    }

    bool MakeTarget(IDirect3DDevice9* d, IDirect3DTexture9*& tex, IDirect3DSurface9*& surf)
    {
        return SUCCEEDED(d->CreateTexture(g_w, g_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr))
            && SUCCEEDED(tex->GetSurfaceLevel(0, &surf));
    }

    bool Ensure(IDirect3DDevice9* d)
    {
        if (maskSurf) return true;
        IDirect3DSurface9* bb = nullptr;
        if (FAILED(d->GetRenderTarget(0, &bb))) return false;
        D3DSURFACE_DESC desc;
        bb->GetDesc(&desc);
        bb->Release();
        g_w = desc.Width; g_h = desc.Height;
        if (MakeTarget(d, maskTex, maskSurf) && MakeTarget(d, ringTex, ringSurf) && SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &stateBlock)))
            return true;
        ReleaseResources();
        return false;
    }

    struct Saved { IDirect3DSurface9* rt; IDirect3DSurface9* ds; D3DVIEWPORT9 vp; };

    void Begin(IDirect3DDevice9* d, Saved& s, IDirect3DSurface9* into)
    {
        d->GetRenderTarget(0, &s.rt);
        d->GetDepthStencilSurface(&s.ds);
        d->GetViewport(&s.vp);
        stateBlock->Capture();
        d->SetRenderTarget(0, into);
        d->SetDepthStencilSurface(nullptr);
    }

    void End(IDirect3DDevice9* d, Saved& s)
    {
        stateBlock->Apply();
        d->SetRenderTarget(0, s.rt);
        d->SetDepthStencilSurface(s.ds);
        d->SetViewport(&s.vp);
        Release(s.rt);
        Release(s.ds);
    }

    // Re-issues the draw that just happened as a flat color into the mask.
    void Stamp(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc)
    {
        if (!Ensure(d)) return;
#ifdef OUTLINE_DEBUG
        ++st.stamped;
#endif
        DWORD blend, test;
        d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
        d->GetRenderState(D3DRS_ALPHATESTENABLE, &test);

        Saved s;
        Begin(d, s, maskSurf);
        if (!g_stamped)
        {
            d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
            g_stamped = true;
        }
        const float* c = g_t[g_cur].rgb;
        d->SetPixelShader(nullptr);
        d->SetRenderState(D3DRS_ZENABLE, FALSE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        if (blend && !test)                                        // translucent layer: keep only its mostly-opaque texels
        {
            d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
            d->SetRenderState(D3DRS_ALPHAREF, 0x80);
        }
        d->SetRenderState(D3DRS_FOGENABLE, FALSE);
        d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        d->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, int(c[0] * 255.0f + 0.5f), int(c[1] * 255.0f + 0.5f), int(c[2] * 255.0f + 0.5f)));
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, blend || test ? D3DTA_TEXTURE : D3DTA_TFACTOR);   // keep cutouts (hair, cloth edges)
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        origDip(d, t, bv, mi, nv, si, pc);
        End(d, s);
    }

    struct Vtx { float x, y, z, rhw, u, v; };

    void Quad(IDirect3DDevice9* d, float du, float dv)
    {
        const float r = static_cast<float>(g_w) - 0.5f, b = static_cast<float>(g_h) - 0.5f;
        const Vtx q[4] = {
            { -0.5f, -0.5f, 0.0f, 1.0f, du,        dv        },
            { r,     -0.5f, 0.0f, 1.0f, 1.0f + du, dv        },
            { -0.5f, b,     0.0f, 1.0f, du,        1.0f + dv },
            { r,     b,     0.0f, 1.0f, 1.0f + du, 1.0f + dv },
        };
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    }

    // Mask -> ring (mask dilated, then its own interior carved out) -> blended over the frame.
    void Composite(IDirect3DDevice9* d)
    {
        g_stamped = false;
        Saved s;
        Begin(d, s, ringSurf);

        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        d->SetRenderState(D3DRS_ZENABLE, FALSE);
        d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_FOGENABLE, FALSE);
        d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetTexture(0, maskTex);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        d->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        static const float dir[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {0.7071f,0.7071f}, {-0.7071f,0.7071f}, {0.7071f,-0.7071f}, {-0.7071f,-0.7071f} };
        for (int ring = 0; ring < 2; ++ring)
        {
            const float r = ring ? cfg.thickness * 0.5f : cfg.thickness;
            for (int i = 0; i < 8; ++i)
                Quad(d, dir[i][0] * r / g_w, dir[i][1] * r / g_h);
        }
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        Quad(d, 0.0f, 0.0f);

        d->SetRenderTarget(0, s.rt);
        d->SetDepthStencilSurface(s.ds);
        d->SetViewport(&s.vp);
        d->SetTexture(0, ringTex);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(int(cfg.opacity * 255.0f + 0.5f), 255, 255, 255));
        d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
        Quad(d, 0.0f, 0.0f);

        stateBlock->Apply();
        Release(s.rt);
        Release(s.ds);
    }

    // ---- hooks
    HRESULT STDMETHODCALLTYPE DipDetour(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc)
    {
        const HRESULT hr = origDip(d, t, bv, mi, nv, si, pc);
        if (g_cur >= 0 && g_draw)
        {
#ifdef OUTLINE_DEBUG
            ++st.draws;
            if (g_type >= 0 && g_type < 6) { ++st.dt[g_type]; st.prim[g_type] += pc; }
#endif
            Stamp(d, t, bv, mi, nv, si, pc);
        }
        return hr;
    }

    // The scene keeps the batch being drawn at +0x3300 and its model at +0x3310.
    inline void EnterBatch(void* scene)
    {
        if (!g_n) return;
        g_cur = Find(*reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(scene) + 0x3310));
#ifdef OUTLINE_DEBUG
        ++st.batches;
        g_type = **reinterpret_cast<int**>(reinterpret_cast<uintptr_t>(scene) + 0x3300);
        if (g_cur >= 0) { ++st.matched; if (g_type >= 0 && g_type < 6) ++st.mt[g_type]; }
#endif
    }

    void __fastcall ModelDrawDetour(void* desc, uint32_t edx)
    {
        g_draw = true;
        origModelDraw(desc, edx);
        g_draw = false;
    }

    template <int I> int __fastcall BatchDetour(void* scene, void*)
    {
        EnterBatch(scene);
        const int r = reinterpret_cast<BatchFn>(origBatch[I])(scene);
        g_cur = -1;
        return r;
    }

    int __fastcall BatchDetour2(void* scene, void*, uint32_t a, uint32_t b)
    {
        EnterBatch(scene);
        const int r = reinterpret_cast<Batch2Fn>(origBatch[2])(scene, a, b);
        g_cur = -1;
        return r;
    }

    bool dipHooked;

    void HookDevice(IDirect3DDevice9* d)
    {
        void** vtbl = *reinterpret_cast<void***>(d);
        if (MH_CreateHook(vtbl[kDipVtable], reinterpret_cast<void*>(&DipDetour), reinterpret_cast<void**>(&origDip)) == MH_OK)
            MH_EnableHook(vtbl[kDipVtable]);
        dipHooked = true;
    }

    int __fastcall RenderWorldDetour(void* self, void*)
    {
        void* L = GameContext();
        if (Valid(reinterpret_cast<uint32_t>(L)) && L != luaState)
        {
            luaState = L;
            GameOpenLib(L, "UnitOutline", luaReg, 0);              // a UI reload makes a new Lua state
        }
        IDirect3DDevice9* d = Device();
        if (d && !dipHooked) HookDevice(d);

        Select();
#ifdef OUTLINE_DEBUG
        ++st.frames;
        if (g_n) ++st.selected;
        FlushStats();
#endif
        const int r = origRenderWorld(self);
        if (g_stamped && d) Composite(d);
        g_n = 0;
        return r;
    }

    void __fastcall ReleaseDetour(uint32_t self, void*, int flag)
    {
        ReleaseResources();
        origRelease(self, flag);
    }

    bool Hook(uintptr_t target, void* detour, void* original)
    {
        void* t = reinterpret_cast<void*>(target);
        return MH_CreateHook(t, detour, reinterpret_cast<void**>(original)) == MH_OK && MH_EnableHook(t) == MH_OK;
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    if (MH_Initialize() != MH_OK) return FALSE;

    void* detour[6] = {
        reinterpret_cast<void*>(&BatchDetour<0>), reinterpret_cast<void*>(&BatchDetour<1>), reinterpret_cast<void*>(&BatchDetour2),
        reinterpret_cast<void*>(&BatchDetour<3>), reinterpret_cast<void*>(&BatchDetour<4>), reinterpret_cast<void*>(&BatchDetour<5>),
    };
    bool ok = Hook(kRenderWorld, reinterpret_cast<void*>(&RenderWorldDetour), &origRenderWorld)
           && Hook(kReleaseD3d, reinterpret_cast<void*>(&ReleaseDetour), &origRelease)
           && Hook(kModelDraw, reinterpret_cast<void*>(&ModelDrawDetour), &origModelDraw);
    for (int i = 0; i < 6 && ok; ++i)
        ok = Hook(kBatch[i], detour[i], &origBatch[i]);
    return ok ? TRUE : FALSE;
}
