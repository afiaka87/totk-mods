// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support - low-level on-screen text draw path.
// Original work Copyright (C) aquacluck and the totk-lotuskit contributors
//   <https://github.com/aquacluck/totk-lotuskit>
// Modifications Copyright (C) HanSoloBlaster, afiaka87
//
// Derived from totk-lotuskit (src/program/TextWriter.hpp), GPLv2. This is the
// canonical reconciliation of the nine per-mod copies (F-B2); the per-donor
// evidence for every kept/rejected behavior is in docs/behavior-matrix.md.
//
// Current contract (invariants, not history):
//   * Draw requests are queued as nodes on a per-frame heap; render hooks draw the
//     queue on EVERY composited layer (render-only, idempotent) so whichever layer
//     composites last carries the text - the per-frame wipe lives in resetFrame(),
//     which the overlay ticks once per frame from the input hook. Binding either
//     half to one named layer reintroduces the "overlay vanishes in gameplay"
//     defect (Tool 2D is not composited during normal play).
//   * The native world-draw callback dispatches BEFORE the text frame-heap gate
//     and before text graphics setup: native consumers must keep working when a
//     mod never initializes text (boot-proven by Self Recall v1.0.1).
//   * Queued text is drawn via "%s", never as a format string.
//   * All text state allocates from a stolen game heap (the mod owns none); every
//     allocation failure is guarded and reported through totk::ui::emitDiagnostic.

#pragma once
#include <atomic>
#if SURVEY_STARTUP_DIAGNOSTIC
#include "SurveyStartupDiagnostic.hpp"
#endif
#include "totk/engine/SymResolve.hpp"  // pulls exlaunch's <lib.hpp> + EXL_SYM_RESOLVE

#include <nn/nn.h>
#include <nn/util.h>
#include <heap/seadHeap.h>
#include <heap/seadFrameHeap.h>
#include <math/seadVector.h>

// agl+gfx include
#include <common/aglDrawContext.h>
#include <lyr/aglLayer.h>
#include <lyr/aglRenderInfo.h>
#include <gfx/seadColor.h>
#include <gfx/seadDrawContext.h>
#include <gfx/seadTextWriter.h>

#include "totk/ui/Diagnostics.hpp"
#include "totk/ui/OverlayLayout.hpp"

namespace lotuskit {
    class TextWriterExt;
    using TextWriterDrawCallback = void(TextWriterExt*, sead::Vector2f*);

    struct TextWriterDrawNode {
        // nodes drawn sequentially, invoking fn if set (eg for positioning) and drawing outputText if set
        char* outputText;
        TextWriterDrawCallback* fn;
        std::atomic<TextWriterDrawNode*> next;
        sead::Color4f color;
        float scale;
        bool centerHorizontally;
        float centerX;
    };

    struct TextWriterFrame {
        static constexpr size_t MAX_DRAWLISTS = 8;
        inline static std::atomic<TextWriterDrawNode*> drawLists[MAX_DRAWLISTS] = {0}; // independent chains of calls (eg for different relative positionings)
        inline static sead::FrameHeap* heap = nullptr; // draw state is wiped each frame
        nn::os::MutexType drawListLock;
    };

    struct TextWriterToastNode {
        char* outputText;
        TextWriterDrawCallback* fn;
        u32 ttlFrames;
    };

    class TextWriterExt; // sead::TextWriter subclass with some bonus calls

    class TextWriter {
        public:
        inline static sead::Color4f defaultColor = {1.0, 1.0, 1.0, 1.0}; // pure white (HanSoloBlaster v0.27.2 readout: all on-screen text white)
        inline static sead::Color4f shadowColor = {0, 0, 0, 1}; // black
        inline static void setDefaultColor(const sead::Color4f& v) { defaultColor = v; }
        inline static void setShadowColor(const sead::Color4f& v) { shadowColor = v; }
        inline static void invertColors() { std::swap(defaultColor, shadowColor); }

        inline static void printf(size_t drawList_i, const char* fmt, auto&&... args) {
            char buf[2000];
            nn::util::SNPrintf(buf, sizeof(buf), fmt, std::forward<decltype(args)>(args)...);
            appendNewDrawNode(drawList_i, buf, nullptr);
        }
        inline static void printf(size_t drawList_i, float scale, const sead::Color4f& color, const char* fmt, auto&&... args) {
            char buf[2000];
            nn::util::SNPrintf(buf, sizeof(buf), fmt, std::forward<decltype(args)>(args)...);
            appendNewDrawNode(drawList_i, buf, nullptr, scale, &color);
        }
        inline static void appendCallback(size_t drawList_i, TextWriterDrawCallback* fn) {
            appendNewDrawNode(drawList_i, nullptr, fn); // caller owns fn lifetime
        }
        inline static void toastf(u32 ttlFrames, const char* fmt, auto&&... args) {
            TextWriterToastNode* newNode = appendNewToastNode(ttlFrames);
            if (newNode) {
                // set outputText
                char buf[2000];
                nn::util::SNPrintf(buf, sizeof(buf), fmt, std::forward<decltype(args)>(args)...);
                size_t n = strlen(buf) + 1;
                newNode->outputText = (char*)debugDrawerInternalHeap->alloc(n);
                // heap full -> leave outputText null (drawToasts null-checks it) instead of
                // memcpy'ing through a null pointer. The empty toast simply ages out.
                if (newNode->outputText != nullptr) {
                    std::memcpy(newNode->outputText, buf, n);
                }
            }
        }

        // toast storage: repeated draw calls for some duration, not part of any drawlist.
        // TTL/slot policy is specified and host-tested in totk/ui/OverlayLayout.hpp.
        static constexpr size_t MAX_TOASTS = totk::ui::tuning::kMaxToasts;
        inline static std::atomic<TextWriterToastNode*> toasts[MAX_TOASTS] = {0};

        // buffer+draw impl
        static TextWriterFrame frame;
        inline static sead::Heap* debugDrawerInternalHeap = nullptr;
        inline static void assignHeap(sead::Heap* heap) {
            // assert only called once
            debugDrawerInternalHeap = heap;
        }
        inline static void createFrameHeap() {
            // assert only called once
            using impl_t = sead::FrameHeap* (size_t, const sead::SafeString&, sead::Heap*, s32, sead::Heap::HeapDirection, bool);
            auto impl = EXL_SYM_RESOLVE<impl_t*>("sead::FrameHeap::create");
            frame.heap = impl(0x4000, "lotuskit::TextWriter", debugDrawerInternalHeap, 8, (sead::Heap::HeapDirection)1, /* doThreadsafeImpl? */ false); // 1 = forward XXX what are args 4+6?

            nn::os::InitializeMutex(&frame.drawListLock, true, 0);
        }
        static void appendNewDrawNode(size_t drawList_i, const char* text = nullptr,
                                      TextWriterDrawCallback* fn = nullptr,
                                      float scale = 0,
                                      const sead::Color4f* color = nullptr,
                                      bool centerHorizontally = false,
                                      float centerX = 0.0f);
        static void appendCenteredDrawNode(size_t drawList_i, const char* text,
                                           TextWriterDrawCallback* fn, float centerX,
                                           float scale = 0,
                                           const sead::Color4f* color = nullptr) {
            appendNewDrawNode(drawList_i, text, fn, scale, color, true, centerX);
        }
        static TextWriterToastNode* appendNewToastNode(u32 ttlFrames);
        static void drawFrame(TextWriterExt*);   // render only; no longer wipes the queue
        static void drawToasts(TextWriterExt*);  // render only; no longer ages toasts
        static void resetFrame();                // wipe queue + age toasts, once per frame
    };

    class TextWriterExt: public sead::TextWriter {
        public:
        void getCursorFromTopLeftImpl(sead::Vector2f* pos) const {
            // always 720p sooooo just make it work
            pos->x = this->mCursor.x + 640.0;
            pos->y = 360.0 - this->mCursor.y;
        }
        void pprintf(sead::Vector2f &pos, const sead::Color4f& color, const char* fmt, auto&&... args) {
            // pretty print: text shadow drawn first
            this->mColor = lotuskit::TextWriter::shadowColor;
            this->setCursorFromTopLeft(pos);
            this->printf(fmt, std::forward<decltype(args)>(args)...);

            // main draw
            this->mColor = color;
            pos.x -= 1.0;
            pos.y -= 1.0;
            this->setCursorFromTopLeft(pos);
            this->printf(fmt, std::forward<decltype(args)>(args)...);

            // remember position for future calls
            this->getCursorFromTopLeftImpl(&pos);
            pos.x += 1.0;
            pos.y += 1.0;
        }
    };

    namespace DebugDrawHooks {
        // Writer-context world draws (receive the frame's TextWriterExt after text
        // graphics setup). Consumers filter to one verified layer themselves so a
        // probe can never accidentally render through mismatched cameras.
        using WorldDrawCallback = void(*)(agl::lyr::Layer*, const agl::lyr::RenderInfo&,
                                          TextWriterExt*);
        // Native 3D consumers (rope/chain renderers, the recall ribbon) run before
        // TextWriter installs its own shader/state. The callback is responsible for
        // applying a complete game-proven graphics context.
        using NativeWorldDrawCallback = void(*)(agl::lyr::Layer*,
                                                const agl::lyr::RenderInfo&);
        inline WorldDrawCallback g_worldDrawCallback = nullptr;
        inline NativeWorldDrawCallback g_nativeWorldDrawCallback = nullptr;
        inline std::atomic<bool> g_textVisible = true;
        inline std::uint32_t g_primitiveUniformBufferBytes = 0;
        using GraphicsPreflight = bool(*)(sead::Heap*);
        inline GraphicsPreflight g_graphicsPreflight = nullptr;
        inline std::atomic<bool> g_graphicsReady{false};
        inline void setGraphicsPreflight(GraphicsPreflight callback) {
            g_graphicsPreflight = callback;
        }
        inline bool graphicsReady() {
            return g_graphicsReady.load(std::memory_order_acquire);
        }
        inline void setWorldDrawCallback(WorldDrawCallback callback) {
            g_worldDrawCallback = callback;
        }
        inline void setNativeWorldDrawCallback(NativeWorldDrawCallback callback) {
            g_nativeWorldDrawCallback = callback;
        }
        inline void setTextVisible(bool visible) {
            g_textVisible.store(visible, std::memory_order_release);
        }
        inline bool textVisible() {
            return g_textVisible.load(std::memory_order_acquire);
        }
        inline void setPrimitiveUniformBufferBytes(std::uint32_t bytes) {
            g_primitiveUniformBufferBytes = bytes;
        }

        struct GraphicsModuleCreateArg {
            u8 _whatever[0xb4c];
            s32 value0;
            u8 _whatever2[0x10];
            s32 value1;
        };
        using InitDebugDrawers = void(sead::Heap*, GraphicsModuleCreateArg&);
        void initializeDebugDrawers(const GraphicsModuleCreateArg* original);

        HOOK_DEFINE_INLINE(BootupInitDebugDrawersHook) {
            static constexpr auto s_name = "agl::create_arg";
            static void Callback(exl::hook::InlineCtx* ctx) {
                initializeDebugDrawers(reinterpret_cast<GraphicsModuleCreateArg*>(ctx->X[1]));
            }
        };

        HOOK_DEFINE_INLINE(DebugDrawLayerMaskHook) {
            static constexpr auto s_name = "agl::lyr::RenderDisplay::drawLayer_::ensure_font"; // hacks
            static void Callback(exl::hook::InlineCtx* ctx) {
                // allow any layer's draw pass to proceed into DebugDrawHook
                auto* layer = (agl::lyr::Layer*)(ctx->X[21]);
                ctx->W[8] = 0x28; // satisfy check to call agl::lyr::Layer::drawDebugInfo_
                layer->mRenderFlags |= 1 << 13; // satisfy check inside vanilla drawDebugInfo_
            }
        };

        HOOK_DEFINE_TRAMPOLINE(DebugDrawHook) {
            static constexpr auto s_name = "agl::lyr::Layer::drawDebugInfo_";
            static void Callback(agl::lyr::Layer* layer, const agl::lyr::RenderInfo& info) {
#if SURVEY_STARTUP_DIAGNOSTIC
                static std::atomic<unsigned> visits{};
                const auto visit = visits.fetch_add(1);
                const auto trace = [visit](const char* stage) {
                    if (visit < 24) zonai_survey::engine::startup_diagnostic::mark(stage, visit);
                };
                trace("survey-draw-enter");
#endif
                auto* sead_draw_ctx = dynamic_cast<sead::DrawContext*>(info.draw_ctx);
#if SURVEY_STARTUP_DIAGNOSTIC
                trace("survey-draw-cast-returned");
#endif
                if (sead_draw_ctx == nullptr) { return; } // some layers don't carry a sead DrawContext

                // Native 3D consumers dispatch BEFORE the text frame-heap gate: a
                // text-free build never creates that heap, and gating this dispatch
                // behind it silently kills native draws (Self Recall v1.0.1).
                if (g_nativeWorldDrawCallback != nullptr) {
                    g_nativeWorldDrawCallback(layer, info);
                }

                // TEXT half only below: do nothing until the frame heap exists.
#if SURVEY_STARTUP_DIAGNOSTIC
                trace(lotuskit::TextWriter::frame.heap ? "survey-text-heap-ready" : "survey-text-heap-absent");
#endif
                if (lotuskit::TextWriter::frame.heap == nullptr) { return; }
                if (!textVisible()) { return; }

                // 2D: render on EVERY composited layer (no name filter) so whichever layer
                // composites LAST each frame carries our text on top. Render-only calls are
                // idempotent (per-frame wipe/aging is in resetFrame), so the overdraw is harmless.
                sead::TextWriter::setupGraphics(sead_draw_ctx);
#if SURVEY_STARTUP_DIAGNOSTIC
                trace("survey-text-setup-returned");
#endif
                sead::TextWriter _writer(sead_draw_ctx, info.viewport);
#if SURVEY_STARTUP_DIAGNOSTIC
                trace("survey-text-construct-returned");
#endif
                TextWriterExt* writer = (TextWriterExt*)(&_writer);
                if (g_worldDrawCallback != nullptr) {
                    g_worldDrawCallback(layer, info, writer);
                }
                lotuskit::TextWriter::drawFrame(writer);
                lotuskit::TextWriter::drawToasts(writer);
#if SURVEY_STARTUP_DIAGNOSTIC
                trace("survey-text-draw-returned");
#endif
            }
        };

    } // ns

} // ns
