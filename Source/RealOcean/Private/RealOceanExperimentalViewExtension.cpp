#include "RealOceanExperimentalViewExtension.h"

#include "HAL/IConsoleManager.h"
#include "GPUProfiler.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

#include "RealOceanHalfResCausticsShaders.h"
#include "RealOceanFoamClipmapShaders.h"
#include "RealOceanRenderBridge.h"
#include "RealOceanReleaseConfig.h"

namespace
{
    DECLARE_GPU_STAT(RealOceanHalfResProbe);
    DECLARE_GPU_STAT(RealOceanHalfResCaustics);
    DECLARE_GPU_STAT(RealOceanFoamClipmap);

    static TAutoConsoleVariable<int32> CVarRealOceanExperimentalHalfResProbe(
        TEXT("r.RealOcean.ExperimentalHalfResProbe"),
        0,
        TEXT("M15 instrumentation probe. Allocates and clears half-resolution R16F before DOF, then returns scene color untouched. 0=off, 1=on."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<int32> CVarRealOceanExperimentalHalfResCaustics(
        TEXT("r.RealOcean.ExperimentalHalfResCaustics"),
        0,
        TEXT("M18 opt-in half-resolution physical caustics prototype. 0=off, 1=depth-aware composite, 2=visualize reconstructed caustics. Development only."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<int32> CVarRealOceanExperimentalGPUFoamClipmap(
        TEXT("r.RealOcean.ExperimentalGPUFoamClipmap"),
        1,
        TEXT("M30-profiled stateful GPU foam clipmap with edge hiding and preset cadence. Requires Cinematic quality, an enabled surface setting, and a bound RO_FoamClipmapTexture material parameter. 0=off, 1=on."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<int32> CVarRealOceanExperimentalHalfResTemporal(
        TEXT("r.RealOcean.ExperimentalHalfResTemporal"),
        1,
        TEXT("M18 temporal accumulation for experimental half-res caustics. Uses previous-view reprojection, depth rejection and neighborhood clipping. 0=off, 1=on."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHalfResDepthSharpness(
        TEXT("r.RealOcean.ExperimentalHalfResDepthSharpness"),
        2.0f,
        TEXT("Depth-aware bilateral upsample rejection strength in 1/meters. Default 2.0."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHalfResCompositeStrength(
        TEXT("r.RealOcean.ExperimentalHalfResCompositeStrength"),
        1.0f,
        TEXT("Experimental M17 composite multiplier. Keep at 1 for A/B equivalence tests."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHistoryWeight(
        TEXT("r.RealOcean.ExperimentalHalfResHistoryWeight"),
        0.68f,
        TEXT("Maximum previous-frame contribution for M17 temporal caustics. Default 0.68."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHistoryDepthRejectMeters(
        TEXT("r.RealOcean.ExperimentalHalfResHistoryDepthRejectMeters"),
        0.75f,
        TEXT("Receiver-depth disagreement in meters used to reject M17 caustics history. Default 0.75."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHistoryMotionRejectPixels(
        TEXT("r.RealOcean.ExperimentalHalfResHistoryMotionRejectPixels"),
        24.0f,
        TEXT("Previous-view screen displacement at which temporal history is strongly rejected. Default 24 pixels."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHistoryClampExpansion(
        TEXT("r.RealOcean.ExperimentalHalfResHistoryClampExpansion"),
        0.15f,
        TEXT("Neighborhood min/max expansion for temporal caustics clipping. Fraction of local range. Default 0.15."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalPatternPhaseRejectRadians(
        TEXT("r.RealOcean.ExperimentalHalfResPatternPhaseRejectRadians"),
        0.45f,
        TEXT("M34 wave-phase delta in radians that fully rejects temporal caustics history. Deltas below 0.05 rad keep full confidence. Default 0.45."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalSunDirectionRejectDegrees(
        TEXT("r.RealOcean.ExperimentalHalfResSunDirectionRejectDegrees"),
        0.50f,
        TEXT("M34 directional-light angular change in degrees that fully rejects temporal caustics history. Default 0.50 degrees."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalHistoryRadianceRejectRatio(
        TEXT("r.RealOcean.ExperimentalHalfResHistoryRadianceRejectRatio"),
        0.75f,
        TEXT("M34 relative caustic-luminance disagreement that fully rejects local history. Default 0.75."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<int32> CVarRealOceanExperimentalSceneVelocity(
        TEXT("r.RealOcean.ExperimentalHalfResSceneVelocity"),
        1,
        TEXT("M18 velocity-aware temporal reprojection. 0=camera/depth reprojection only, 1=use encoded scene velocity when present and fall back to camera reprojection otherwise."),
        ECVF_RenderThreadSafe);

    static TAutoConsoleVariable<float> CVarRealOceanExperimentalObjectVelocityRejectPixels(
        TEXT("r.RealOcean.ExperimentalHalfResObjectVelocityRejectPixels"),
        32.0f,
        TEXT("M18 relative object-motion magnitude in pixels used to reduce caustics history confidence. Default 32 pixels."),
        ECVF_RenderThreadSafe);

    struct FRealOceanTemporalHistory
    {
        TRefCountPtr<IPooledRenderTarget> Texture;
        double LastOceanTimeSeconds = -1.0;
        FVector3f LastSunDirectionToSourceWS = FVector3f(0.0f, 0.0f, 1.0f);
        float LastDominantSwellPeriodSeconds = -1.0f;
        float LastWindSeaPeakPeriodSeconds = -1.0f;
        float LastSwellSignificantWaveHeightMeters = -1.0f;
        float LastWindSeaSignificantWaveHeightMeters = -1.0f;
        float LastTravelDirectionDegrees = 0.0f;
        float LastWindDirectionDegrees = 0.0f;
        uint32 LastTouchedFrameNumber = 0;
    };

    struct FRealOceanFoamSceneHistory
    {
        FRealOceanFoamClipmapHistory History;
        uint32 LastUpdateFrameNumber = MAX_uint32;
        uint32 LastTouchedFrameNumber = 0;
    };

    static float M34SmoothStep(float Edge0, float Edge1, float X)
    {
        const float Denom = FMath::Max(Edge1 - Edge0, KINDA_SMALL_NUMBER);
        const float T = FMath::Clamp((X - Edge0) / Denom, 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    static float M34ShortestAngleDegrees(float A, float B)
    {
        return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
    }

    static float M34PatternHistoryConfidence(
        const FRealOceanTemporalHistory& History,
        const FRealOceanRenderState& State,
        float PhaseRejectRadians,
        float SunRejectDegrees)
    {
        if (History.LastOceanTimeSeconds < 0.0)
        {
            return 0.0f;
        }

        const double DeltaTime = State.OceanTimeSeconds - History.LastOceanTimeSeconds;
        if (DeltaTime < 0.0 || DeltaTime > 0.25)
        {
            return 0.0f;
        }

        const float MinPeriod = FMath::Max(0.25f, FMath::Min(
            FMath::Max(State.DominantSwellPeriodSeconds, 0.25f),
            FMath::Max(State.WindSeaPeakPeriodSeconds, 0.25f)));
        const float PhaseDelta = static_cast<float>(2.0 * PI * FMath::Abs(DeltaTime) / MinPeriod);
        const float PhaseReject = FMath::Max(PhaseRejectRadians, 0.10f);
        const float PhaseConfidence = 1.0f - M34SmoothStep(0.05f, PhaseReject, PhaseDelta);

        const FVector3f CurrentSun = State.SunDirectionToSourceWS.GetSafeNormal();
        const FVector3f PreviousSun = History.LastSunDirectionToSourceWS.GetSafeNormal();
        const float SunDot = FMath::Clamp(FVector3f::DotProduct(CurrentSun, PreviousSun), -1.0f, 1.0f);
        const float SunDeltaDegrees = FMath::RadiansToDegrees(FMath::Acos(SunDot));
        const float SunReject = FMath::Max(SunRejectDegrees, 0.10f);
        const float SunConfidence = 1.0f - M34SmoothStep(0.02f, SunReject, SunDeltaDegrees);

        // Weather/profile edits can move the physical caustic field even when ocean time is continuous.
        // Fade history instead of allowing the old pattern to survive a sudden period/height/direction edit.
        float ConfigurationMetric = 0.0f;
        if (History.LastDominantSwellPeriodSeconds > 0.0f && History.LastWindSeaPeakPeriodSeconds > 0.0f)
        {
            const float SwellPeriodRel = FMath::Abs(State.DominantSwellPeriodSeconds - History.LastDominantSwellPeriodSeconds)
                / FMath::Max(History.LastDominantSwellPeriodSeconds, 0.25f);
            const float WindPeriodRel = FMath::Abs(State.WindSeaPeakPeriodSeconds - History.LastWindSeaPeakPeriodSeconds)
                / FMath::Max(History.LastWindSeaPeakPeriodSeconds, 0.25f);
            const float SwellHeightDelta = FMath::Abs(State.SwellSignificantWaveHeightMeters - History.LastSwellSignificantWaveHeightMeters);
            const float WindHeightDelta = FMath::Abs(State.WindSeaSignificantWaveHeightMeters - History.LastWindSeaSignificantWaveHeightMeters);
            const float TravelDirectionDelta = M34ShortestAngleDegrees(State.TravelDirectionDegrees, History.LastTravelDirectionDegrees);
            const float WindDirectionDelta = M34ShortestAngleDegrees(State.WindDirectionDegrees, History.LastWindDirectionDegrees);

            ConfigurationMetric = FMath::Max(
                FMath::Max(SwellPeriodRel / 0.05f, WindPeriodRel / 0.05f),
                FMath::Max(
                    FMath::Max(SwellHeightDelta / 0.10f, WindHeightDelta / 0.10f),
                    FMath::Max(TravelDirectionDelta, WindDirectionDelta) / 3.0f));
        }
        const float ConfigurationConfidence = FMath::Square(FMath::Clamp(1.0f - ConfigurationMetric, 0.0f, 1.0f));
        return FMath::Clamp(PhaseConfidence * SunConfidence * ConfigurationConfidence, 0.0f, 1.0f);
    }

    class FRealOceanExperimentalViewExtension final : public FSceneViewExtensionBase
    {
    public:
        FRealOceanExperimentalViewExtension(const FAutoRegister& AutoRegister)
            : FSceneViewExtensionBase(AutoRegister)
        {
        }

        virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
        virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
        virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

        virtual void SubscribeToPostProcessingPass(
            EPostProcessingPass Pass,
            const FSceneView& InView,
            FPostProcessingPassDelegateArray& InOutPassCallbacks,
            bool bIsPassEnabled) override
        {
#if UE_BUILD_SHIPPING
            constexpr bool bProbe = false;
            constexpr bool bHalfResCaustics = RealOceanRelease::UsesRDGCaustics();
            constexpr bool bGPUFoamClipmap = RealOceanRelease::UsesGPUFoamClipmap();
#else
            const bool bProbe = CVarRealOceanExperimentalHalfResProbe.GetValueOnAnyThread() != 0;
            const bool bHalfResCaustics = CVarRealOceanExperimentalHalfResCaustics.GetValueOnAnyThread() != 0;
            const bool bGPUFoamClipmap = CVarRealOceanExperimentalGPUFoamClipmap.GetValueOnAnyThread() != 0;
#endif

            if (Pass == EPostProcessingPass::BeforeDOF && bIsPassEnabled && (bProbe || bHalfResCaustics || bGPUFoamClipmap))
            {
                InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(
                    this,
                    &FRealOceanExperimentalViewExtension::AddExperimentalPass_RenderThread));
            }
        }

    private:
        FRealOceanTemporalHistory& GetHistoryForView(const FSceneView& View)
        {
            FRealOceanTemporalHistory& History = View.State
                ? TemporalHistories.FindOrAdd(View.State)
                : FallbackTemporalHistory;
            History.LastTouchedFrameNumber = GFrameNumberRenderThread;
            return History;
        }

        void PruneStaleHistories()
        {
            constexpr uint32 HistoryRetentionFrames = 600;
            if ((GFrameNumberRenderThread % 300u) != 0u)
            {
                return;
            }

            for (auto It = TemporalHistories.CreateIterator(); It; ++It)
            {
                if (GFrameNumberRenderThread - It.Value().LastTouchedFrameNumber > HistoryRetentionFrames)
                {
                    It.RemoveCurrent();
                }
            }
            for (auto It = FoamSceneHistories.CreateIterator(); It; ++It)
            {
                if (GFrameNumberRenderThread - It.Value().LastTouchedFrameNumber > HistoryRetentionFrames)
                {
                    It.RemoveCurrent();
                }
            }
        }

        FScreenPassTexture AddExperimentalPass_RenderThread(
            FRDGBuilder& GraphBuilder,
            const FSceneView& View,
            const FPostProcessMaterialInputs& Inputs)
        {
#if UE_BUILD_SHIPPING
            constexpr int32 HalfResMode = RealOceanRelease::UsesRDGCaustics() ? 1 : 0;
            constexpr bool bGPUFoamEnabled = RealOceanRelease::UsesGPUFoamClipmap();
            constexpr bool bProbeEnabled = false;
#else
            const int32 HalfResMode = FMath::Clamp(CVarRealOceanExperimentalHalfResCaustics.GetValueOnRenderThread(), 0, 2);
            const bool bGPUFoamEnabled = CVarRealOceanExperimentalGPUFoamClipmap.GetValueOnRenderThread() != 0;
            const bool bProbeEnabled = CVarRealOceanExperimentalHalfResProbe.GetValueOnRenderThread() != 0;
#endif
            PruneStaleHistories();
            const FSceneInterface* Scene = View.Family ? View.Family->Scene : nullptr;
            const FRealOceanRenderState& SceneRenderState = GetRealOceanRenderState_RenderThread(Scene);
            const FVector ViewOriginCm = View.ViewMatrices.GetViewOrigin();
            const FRealOceanRenderState RenderState = BuildRealOceanViewRenderState_RenderThread(SceneRenderState, ViewOriginCm);

            // Each world/scene owns one persistent world-space foam target/history. Update it from the
            // scene-level base state, never from whichever split-screen/SceneCapture callback happens to run
            // first. Per-view RenderState is reserved for view-dependent caustics/depth below. World-space
            // foam does not need a camera-cut reset; center-motion/time discontinuity handles overlap loss.
            if (bGPUFoamEnabled && Scene)
            {
                FRealOceanFoamSceneHistory& FoamSceneHistory = FoamSceneHistories.FindOrAdd(Scene);
                FoamSceneHistory.LastTouchedFrameNumber = GFrameNumberRenderThread;
                if (FoamSceneHistory.LastUpdateFrameNumber != GFrameNumberRenderThread)
                {
                    RDG_EVENT_SCOPE(GraphBuilder, "RealOcean.Experimental.GPUFoamClipmap.M32");
                    RDG_GPU_STAT_SCOPE(GraphBuilder, RealOceanFoamClipmap);
                    AddRealOceanFoamClipmapPass(GraphBuilder, View, SceneRenderState, FoamSceneHistory.History, false);
                    FoamSceneHistory.LastUpdateFrameNumber = GFrameNumberRenderThread;
                }
            }

            if (HalfResMode != 0)
            {
                RDG_EVENT_SCOPE(GraphBuilder, "RealOcean.Experimental.HalfResCaustics.M34");
                RDG_GPU_STAT_SCOPE(GraphBuilder, RealOceanHalfResCaustics);

                FRealOceanTemporalHistory& History = GetHistoryForView(View);
#if UE_BUILD_SHIPPING
                constexpr bool bTemporalRequested = RealOceanRelease::UsesTemporalRDGCaustics();
                constexpr float DepthSharpness = 2.0f;
                constexpr float CompositeStrength = 1.0f;
                constexpr float HistoryWeight = 0.68f;
                constexpr float HistoryDepthRejectMeters = 0.75f;
                constexpr float HistoryMotionRejectPixels = 24.0f;
                constexpr float HistoryClampExpansion = 0.15f;
                constexpr float PatternPhaseRejectRadians = 0.45f;
                constexpr float SunDirectionRejectDegrees = 0.50f;
                constexpr float HistoryRadianceRejectRatio = 0.75f;
                constexpr bool bSceneVelocityEnabled = true;
                constexpr float ObjectVelocityRejectPixels = 32.0f;
#else
                const bool bTemporalRequested = CVarRealOceanExperimentalHalfResTemporal.GetValueOnRenderThread() != 0;
                const float DepthSharpness = CVarRealOceanExperimentalHalfResDepthSharpness.GetValueOnRenderThread();
                const float CompositeStrength = CVarRealOceanExperimentalHalfResCompositeStrength.GetValueOnRenderThread();
                const float HistoryWeight = FMath::Clamp(CVarRealOceanExperimentalHistoryWeight.GetValueOnRenderThread(), 0.0f, 0.98f);
                const float HistoryDepthRejectMeters = FMath::Max(0.01f, CVarRealOceanExperimentalHistoryDepthRejectMeters.GetValueOnRenderThread());
                const float HistoryMotionRejectPixels = FMath::Max(1.0f, CVarRealOceanExperimentalHistoryMotionRejectPixels.GetValueOnRenderThread());
                const float HistoryClampExpansion = FMath::Max(0.0f, CVarRealOceanExperimentalHistoryClampExpansion.GetValueOnRenderThread());
                const float PatternPhaseRejectRadians = FMath::Max(0.10f, CVarRealOceanExperimentalPatternPhaseRejectRadians.GetValueOnRenderThread());
                const float SunDirectionRejectDegrees = FMath::Max(0.10f, CVarRealOceanExperimentalSunDirectionRejectDegrees.GetValueOnRenderThread());
                const float HistoryRadianceRejectRatio = FMath::Max(0.05f, CVarRealOceanExperimentalHistoryRadianceRejectRatio.GetValueOnRenderThread());
                const bool bSceneVelocityEnabled = CVarRealOceanExperimentalSceneVelocity.GetValueOnRenderThread() != 0;
                const float ObjectVelocityRejectPixels = FMath::Max(1.0f, CVarRealOceanExperimentalObjectVelocityRejectPixels.GetValueOnRenderThread());
#endif
                // Stateless views (SceneCapture, thumbnails, transient editor views) must not share temporal
                // history. A camera cut also invalidates the previous-view reprojection immediately.
                const bool bTemporalEnabled = bTemporalRequested && View.State != nullptr;

                const float PatternHistoryConfidence = M34PatternHistoryConfidence(
                    History, RenderState, PatternPhaseRejectRadians, SunDirectionRejectDegrees);

                // Large time discontinuities imply pause/scrub/teleport-like state. Do not blend stale caustics history.
                if (!bTemporalEnabled ||
                    View.bCameraCut ||
                    History.LastOceanTimeSeconds < 0.0 ||
                    RenderState.OceanTimeSeconds < History.LastOceanTimeSeconds ||
                    (RenderState.OceanTimeSeconds - History.LastOceanTimeSeconds) > 0.25f)
                {
                    History.Texture = nullptr;
                }

                History.LastOceanTimeSeconds = RenderState.OceanTimeSeconds;
                History.LastSunDirectionToSourceWS = RenderState.SunDirectionToSourceWS;
                History.LastDominantSwellPeriodSeconds = RenderState.DominantSwellPeriodSeconds;
                History.LastWindSeaPeakPeriodSeconds = RenderState.WindSeaPeakPeriodSeconds;
                History.LastSwellSignificantWaveHeightMeters = RenderState.SwellSignificantWaveHeightMeters;
                History.LastWindSeaSignificantWaveHeightMeters = RenderState.WindSeaSignificantWaveHeightMeters;
                History.LastTravelDirectionDegrees = RenderState.TravelDirectionDegrees;
                History.LastWindDirectionDegrees = RenderState.WindDirectionDegrees;

                return AddRealOceanHalfResCausticsPass(
                    GraphBuilder,
                    View,
                    Inputs,
                    RenderState,
                    HalfResMode,
                    DepthSharpness,
                    CompositeStrength,
                    bTemporalEnabled,
                    HistoryWeight,
                    HistoryDepthRejectMeters,
                    HistoryMotionRejectPixels,
                    HistoryClampExpansion,
                    PatternHistoryConfidence,
                    HistoryRadianceRejectRatio,
                    bSceneVelocityEnabled,
                    ObjectVelocityRejectPixels,
                    &History.Texture);
            }

            const FScreenPassTexture SceneColor = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
            if (!bProbeEnabled || !SceneColor.IsValid())
            {
                return SceneColor;
            }

            RDG_EVENT_SCOPE(GraphBuilder, "RealOcean.Experimental.HalfResProbe");
            RDG_GPU_STAT_SCOPE(GraphBuilder, RealOceanHalfResProbe);

            const FIntPoint FullExtent = SceneColor.Texture->Desc.Extent;
            const FIntPoint HalfExtent(
                FMath::Max(1, FMath::DivideAndRoundUp(FullExtent.X, 2)),
                FMath::Max(1, FMath::DivideAndRoundUp(FullExtent.Y, 2)));

            const FRDGTextureDesc ProbeDesc = FRDGTextureDesc::Create2D(
                HalfExtent,
                PF_R16F,
                FClearValueBinding::White,
                TexCreate_ShaderResource | TexCreate_UAV);

            FRDGTextureRef ProbeTexture = GraphBuilder.CreateTexture(
                ProbeDesc,
                TEXT("RealOcean.Experimental.HalfResProbe"));

            AddClearUAVPass(
                GraphBuilder,
                GraphBuilder.CreateUAV(ProbeTexture),
                1.0f,
                ERDGPassFlags::Compute);

            GraphBuilder.QueueTextureExtraction(
                ProbeTexture,
                &ExtractedProbeTarget,
                ERDGResourceExtractionFlags::None);

            return SceneColor;
        }

        TRefCountPtr<IPooledRenderTarget> ExtractedProbeTarget;
        TMap<const FSceneViewStateInterface*, FRealOceanTemporalHistory> TemporalHistories;
        FRealOceanTemporalHistory FallbackTemporalHistory;
        TMap<const FSceneInterface*, FRealOceanFoamSceneHistory> FoamSceneHistories;
    };

    static TSharedPtr<FRealOceanExperimentalViewExtension, ESPMode::ThreadSafe> GRealOceanExperimentalViewExtension;
}

void StartRealOceanExperimentalViewExtension()
{
#if !UE_BUILD_SHIPPING || REALOCEAN_RELEASE_CAUSTICS_PATH != 0 || REALOCEAN_RELEASE_FOAM_PATH != 0
    if (!GRealOceanExperimentalViewExtension.IsValid())
    {
        GRealOceanExperimentalViewExtension = FSceneViewExtensions::NewExtension<FRealOceanExperimentalViewExtension>();
    }
#endif
}

void StopRealOceanExperimentalViewExtension()
{
    GRealOceanExperimentalViewExtension.Reset();
}
