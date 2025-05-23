// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Cesium3DTilesSelection/Tileset.h"
#include "Cesium3DTilesSelection/ViewState.h"
#include "Cesium3DTilesSelection/ViewUpdateResult.h"
#include "Cesium3DTilesetLoadFailureDetails.h"
#include "CesiumCreditSystem.h"
#include "CesiumEncodedMetadataComponent.h"
#include "CesiumFeaturesMetadataComponent.h"
#include "CesiumGeoreference.h"
#include "CesiumIonServer.h"
#include "CesiumPointCloudShading.h"
#include "CesiumSampleHeightResult.h"
#include "CoreMinimal.h"
#include "CustomDepthParameters.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IHttpRequest.h"
#include "PrimitiveSceneProxy.h"
#include <PhysicsEngine/BodyInstance.h>
#include <atomic>
#include <chrono>
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include <vector>
#include "Cesium3DTileset.generated.h"

#ifdef CESIUM_DEBUG_TILE_STATES
#include <Cesium3DTilesSelection/DebugTileStateDatabase.h>
#endif

class UMaterialInterface;
class ACesiumCartographicSelection;
class ACesiumCameraManager;
class UCesiumBoundingVolumePoolComponent;
class CesiumViewExtension;
struct FCesiumCamera;

namespace Cesium3DTilesSelection {
class Tileset;
class TilesetView;
class TileOcclusionRendererProxyPool;
} // namespace Cesium3DTilesSelection

/**
 * The delegate for OnCesium3DTilesetLoadFailure, which is triggered when
 * the tileset encounters a load error.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
    FCesium3DTilesetLoadFailure,
    const FCesium3DTilesetLoadFailureDetails&);

DECLARE_DELEGATE_ThreeParams(
    FCesiumSampleHeightMostDetailedCallback,
    ACesium3DTileset*,
    const TArray<FCesiumSampleHeightResult>&,
    const TArray<FString>&);

/**
 * The delegate for the Acesium3DTileset::OnTilesetLoaded,
 * which is triggered from UpdateLoadStatus
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCompletedLoadTrigger);

CESIUMRUNTIME_API extern FCesium3DTilesetLoadFailure
    OnCesium3DTilesetLoadFailure;

UENUM(BlueprintType)
enum class ETilesetSource : uint8 {
  /**
   * The tileset will be loaded from Cesium Ion using the provided IonAssetID
   * and IonAccessToken.
   */
  FromCesiumIon UMETA(DisplayName = "From Cesium Ion"),

  /**
   * The tileset will be loaded from the specified Url.
   */
  FromUrl UMETA(DisplayName = "From Url"),

  /**
   * The tileset will be loaded from the georeference ellipsoid.
   */
  FromEllipsoid UMETA(DisplayName = "From Ellipsoid")
};

UENUM(BlueprintType)
enum class EApplyDpiScaling : uint8 { Yes, No, UseProjectDefault };

UCLASS()
class CESIUMRUNTIME_API ACesium3DTileset : public AActor {
  GENERATED_BODY()

public:
  ACesium3DTileset();
  virtual ~ACesium3DTileset();

private:
  UPROPERTY(VisibleAnywhere, Category = "Cesium") USceneComponent* Root;

  UPROPERTY(
      Meta =
          (AllowPrivateAccess,
           DeprecatedProperty,
           DeprecationMessage =
               "Use the Mobility property on the RootComponent instead."))
  TEnumAsByte<EComponentMobility::Type> Mobility_DEPRECATED =
      EComponentMobility::Static;

public:
  UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction))
  EComponentMobility::Type GetMobility() const {
    return this->RootComponent->Mobility;
  }
  UFUNCTION(BlueprintCallable, meta = (DeprecatedFunction))
  void SetMobility(EComponentMobility::Type NewMobility);

  /**
   * @brief Initiates an asynchronous query for the height of this tileset at a
   * list of cartographic positions, where the Longitude (X) and Latitude (Y)
   * are given in degrees. The most detailed available tiles are used to
   * determine each height.
   *
   * The height of the input positions is ignored, unless height sampling fails
   * at that location. The output height is expressed in meters above the
   * ellipsoid (usually WGS84), which should not be confused with a height above
   * mean sea level.
   *
   * @param LongitudeLatitudeHeightArray The cartographic positions for which to
   * sample heights. The Longitude (X) and Latitude (Y) are expressed in
   * degrees, while Height (Z) is given in meters.
   * @param OnHeightsSampled A callback that is invoked in the game thread when
   * heights have been sampled for all positions.
   */
  void SampleHeightMostDetailed(
      const TArray<FVector>& LongitudeLatitudeHeightArray,
      FCesiumSampleHeightMostDetailedCallback OnHeightsSampled);

private:
  /**
   * The designated georeference actor controlling how the actor's
   * coordinate system relates to the coordinate system in this Unreal Engine
   * level.
   *
   * If this is null, the Tileset will find and use the first Georeference
   * Actor in the level, or create one if necessary. To get the active/effective
   * Georeference from Blueprints or C++, use ResolvedGeoreference instead.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      BlueprintGetter = GetGeoreference,
      BlueprintSetter = SetGeoreference,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  TSoftObjectPtr<ACesiumGeoreference> Georeference;

  /**
   * The resolved georeference used by this Tileset. This is not serialized
   * because it may point to a Georeference in the PersistentLevel while this
   * tileset is in a sublevel. If the Georeference property is specified,
   * however then this property will have the same value.
   *
   * This property will be null before ResolveGeoreference is called.
   */
  UPROPERTY(
      Transient,
      VisibleAnywhere,
      BlueprintReadOnly,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  ACesiumGeoreference* ResolvedGeoreference = nullptr;

public:
  /** @copydoc ACesium3DTileset::Georeference */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  TSoftObjectPtr<ACesiumGeoreference> GetGeoreference() const;

  /** @copydoc ACesium3DTileset::Georeference */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  void SetGeoreference(TSoftObjectPtr<ACesiumGeoreference> NewGeoreference);

  /**
   * Resolves the Cesium Georeference to use with this Actor. Returns
   * the value of the Georeference property if it is set. Otherwise, finds a
   * Georeference in the World and returns it, creating it if necessary. The
   * resolved Georeference is cached so subsequent calls to this function will
   * return the same instance.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  ACesiumGeoreference* ResolveGeoreference();

  /**
   * Invalidates the cached resolved georeference, unsubscribing from it and
   * setting it to null. The next time ResolveGeoreference is called, the
   * Georeference will be re-resolved and re-subscribed.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  void InvalidateResolvedGeoreference();

private:
  /**
   * The actor managing this tileset's content attributions.
   *
   * If this is null, the Tileset will find and use the first Credit System
   * Actor in the level, or create one if necessary. To get the active/effective
   * Credit System from Blueprints or C++, use ResolvedCreditSystem instead.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      BlueprintGetter = GetCreditSystem,
      BlueprintSetter = SetCreditSystem,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  TSoftObjectPtr<ACesiumCreditSystem> CreditSystem;

  /**
   * The resolved Credit System used by this Tileset. This is not serialized
   * because it may point to a Credit System in the PersistentLevel while this
   * tileset is in a sublevel. If the CreditSystem property is specified,
   * however then this property will have the same value.
   *
   * This property will be null before ResolveCreditSystem is called.
   */
  UPROPERTY(
      Transient,
      BlueprintReadOnly,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  ACesiumCreditSystem* ResolvedCreditSystem = nullptr;

  /**
   * The actor providing custom cameras for use with this Tileset.
   *
   * If this is null, the Tileset will find and use the first
   * CesiumCameraManager Actor in the level, or create one if necessary. To get
   * the active/effective Camera Manager from Blueprints or C++, use
   * ResolvedCameraManager instead.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      BlueprintGetter = GetCameraManager,
      BlueprintSetter = SetCameraManager,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  TSoftObjectPtr<ACesiumCameraManager> CameraManager;

  /**
   * The resolved Camera Manager used by this Tileset. This is not serialized
   * because it may point to a Camera Manager in the PersistentLevel while this
   * tileset is in a sublevel. If the CameraManager property is specified,
   * however then this property will have the same value.
   *
   * This property will be null before ResolveCameraManager is called.
   */
  UPROPERTY(
      Transient,
      BlueprintReadOnly,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  ACesiumCameraManager* ResolvedCameraManager = nullptr;

  /**
   * The bounding volume pool component that manages occlusion bounding volume
   * proxies.
   */
  UPROPERTY(
      Transient,
      BlueprintReadOnly,
      Category = "Cesium",
      Meta = (AllowPrivateAccess))
  UCesiumBoundingVolumePoolComponent* BoundingVolumePoolComponent = nullptr;

  /**
   * The custom view extension this tileset uses to pull renderer view
   * information.
   */
  TSharedPtr<CesiumViewExtension, ESPMode::ThreadSafe> _cesiumViewExtension =
      nullptr;

public:
  /** @copydoc ACesium3DTileset::CreditSystem */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  TSoftObjectPtr<ACesiumCreditSystem> GetCreditSystem() const;

  /** @copydoc ACesium3DTileset::CreditSystem */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  void SetCreditSystem(TSoftObjectPtr<ACesiumCreditSystem> NewCreditSystem);

  /**
   * Resolves the Cesium Credit System to use with this Actor. Returns
   * the value of the CreditSystem property if it is set. Otherwise, finds a
   * Credit System in the World and returns it, creating it if necessary. The
   * resolved Credit System is cached so subsequent calls to this function will
   * return the same instance.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  ACesiumCreditSystem* ResolveCreditSystem();

  /**
   * Invalidates the cached resolved Credit System, setting it to null. The next
   * time ResolveCreditSystem is called, the Credit System will be re-resolved.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  void InvalidateResolvedCreditSystem();

  /**
   * Whether or not to show this tileset's credits on screen.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium")
  bool ShowCreditsOnScreen = false;

  /** @copydoc ACesium3DTileset::CameraManager */
  UFUNCTION(BlueprintGetter, Category = "Cesium")
  TSoftObjectPtr<ACesiumCameraManager> GetCameraManager() const;

  /** @copydoc ACesium3DTileset::CameraManager */
  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetCameraManager(TSoftObjectPtr<ACesiumCameraManager> NewCameraManager);

  /**
   * Resolves the Cesium Camera Manager to use with this Actor. Returns
   * the value of the CameraManager property if it is set. Otherwise, finds a
   * Camera Manager in the World and returns it, creating it if necessary. The
   * resolved Camera Manager is cached so subsequent calls to this function will
   * return the same instance.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  ACesiumCameraManager* ResolveCameraManager();

  /**
   * Invalidates the cached resolved Camera Manager, setting it to null. The
   * next time ResolveCameraManager is called, the Camera Manager will be
   * re-resolved.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  void InvalidateResolvedCameraManager();

  /**
   * The maximum number of pixels of error when rendering this tileset.
   *
   * This is used to select an appropriate level-of-detail: A low value
   * will cause many tiles with a high level of detail to be loaded,
   * causing a finer visual representation of the tiles, but with a
   * higher performance cost for loading and rendering. A higher value will
   * cause a coarser visual representation, with lower performance
   * requirements.
   *
   * When a tileset uses the older layer.json / quantized-mesh format rather
   * than 3D Tiles, this value is effectively divided by 8.0. So the default
   * value of 16.0 corresponds to the standard value for quantized-mesh terrain
   * of 2.0.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetMaximumScreenSpaceError,
      BlueprintSetter = SetMaximumScreenSpaceError,
      Category = "Cesium|Level of Detail",
      meta = (ClampMin = 0.0))
  double MaximumScreenSpaceError = 16.0;

  /**
   * Scale Level-of-Detail by Display DPI. This increases the performance for
   * mobile devices and high DPI screens.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Level of Detail")
  EApplyDpiScaling ApplyDpiScaling = EApplyDpiScaling::UseProjectDefault;

  /**
   * Whether to preload ancestor tiles.
   *
   * Setting this to true optimizes the zoom-out experience and provides more
   * detail in newly-exposed areas when panning. The down side is that it
   * requires loading more tiles.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Tile Loading")
  bool PreloadAncestors = true;

  /**
   * Whether to preload sibling tiles.
   *
   * Setting this to true causes tiles with the same parent as a rendered tile
   * to be loaded, even if they are culled. Setting this to true may provide a
   * better panning experience at the cost of loading more tiles.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Tile Loading")
  bool PreloadSiblings = true;

  /**
   * Whether to unrefine back to a parent tile when a child isn't done loading.
   *
   * When this is set to true, the tileset will guarantee that the tileset will
   * never be rendered with holes in place of tiles that are not yet loaded,
   * even though the tile that is rendered instead may have low resolution. When
   * false, overall loading will be faster, but newly-visible parts of the
   * tileset may initially be blank.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Tile Loading")
  bool ForbidHoles = false;

  /**
   * The maximum number of tiles that may be loaded at once.
   *
   * When new parts of the tileset become visible, the tasks to load the
   * corresponding tiles are put into a queue. This value determines how
   * many of these tasks are processed at the same time. A higher value
   * may cause the tiles to be loaded and rendered more quickly, at the
   * cost of a higher network- and processing load.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Tile Loading",
      meta = (ClampMin = 0))
  int32 MaximumSimultaneousTileLoads = 20;

  /**
   * @brief The maximum number of bytes that may be cached.
   *
   * Note that this value, even if 0, will never
   * cause tiles that are needed for rendering to be unloaded. However, if the
   * total number of loaded bytes is greater than this value, tiles will be
   * unloaded until the total is under this number or until only required tiles
   * remain, whichever comes first.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Tile Loading")
  int64 MaximumCachedBytes = 256 * 1024 * 1024;

  /**
   * The number of loading descendents a tile should allow before deciding to
   * render itself instead of waiting.
   *
   * Setting this to 0 will cause each level of detail to be loaded
   * successively. This will increase the overall loading time, but cause
   * additional detail to appear more gradually. Setting this to a high value
   * like 1000 will decrease the overall time until the desired level of detail
   * is achieved, but this high-detail representation will appear at once, as
   * soon as it is loaded completely.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Tile Loading",
      meta = (ClampMin = 0))
  int32 LoadingDescendantLimit = 20;

  /**
   * Whether to cull tiles that are outside the frustum.
   *
   * By default this is true, meaning that tiles that are not visible with the
   * current camera configuration will be ignored. It can be set to false, so
   * that these tiles are still considered for loading, refinement and
   * rendering.
   *
   * This will cause more tiles to be loaded, but helps to avoid holes and
   * provides a more consistent mesh, which may be helpful for physics.
   *
   * Note that this will always be disabled if UseLodTransitions is set to true.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Tile Culling",
      Meta = (EditCondition = "!UseLodTransitions", EditConditionHides))
  bool EnableFrustumCulling = true;

  /**
   * Whether to cull tiles that are occluded by fog.
   *
   * This does not refer to the atmospheric fog of the Unreal Engine,
   * but to an internal representation of fog: Depending on the height
   * of the camera above the ground, tiles that are far away (close to
   * the horizon) will be culled when this flag is enabled.
   *
   * Note that this will always be disabled if UseLodTransitions is set to true.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Tile Culling",
      Meta = (EditCondition = "!UseLodTransitions", EditConditionHides))
  bool EnableFogCulling = true;

  /**
   * Whether a specified screen-space error should be enforced for tiles that
   * are outside the frustum or hidden in fog.
   *
   * When "Enable Frustum Culling" and "Enable Fog Culling" are both true, tiles
   * outside the view frustum or hidden in fog are effectively ignored, and so
   * their level-of-detail doesn't matter. And in this scenario, this property
   * is ignored.
   *
   * However, when either of those flags are false, these "would-be-culled"
   * tiles continue to be processed, and the question arises of how to handle
   * their level-of-detail. When this property is false, refinement terminates
   * at these tiles, no matter what their current screen-space error. The tiles
   * are available for physics, shadows, etc., but their level-of-detail may
   * be very low.
   *
   * When set to true, these tiles are refined until they achieve the specified
   * "Culled Screen Space Error". This allows control over the minimum quality
   * of these would-be-culled tiles.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Tile Culling")
  bool EnforceCulledScreenSpaceError = false;

  /**
   * The screen-space error to be enforced for tiles that are outside the view
   * frustum or hidden in fog.
   *
   * When "Enable Frustum Culling" and "Enable Fog Culling" are both true, tiles
   * outside the view frustum or hidden in fog are effectively ignored, and so
   * their level-of-detail doesn't matter. And in this scenario, this property
   * is ignored.
   *
   * However, when either of those flags are false, these "would-be-culled"
   * tiles continue to be processed, and the question arises of how to handle
   * their level-of-detail. When "Enforce Culled Screen Space Error" is false,
   * this property is ignored and refinement terminates at these tiles, no
   * matter what their current screen-space error. The tiles are available for
   * physics, shadows, etc., but their level-of-detail may be very low.
   *
   * When set to true, these tiles are refined until they achieve the
   * screen-space error specified by this property.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Tile Culling",
      meta = (EditCondition = "EnforceCulledScreenSpaceError", ClampMin = 0.0))
  double CulledScreenSpaceError = 64.0;

  // This mirrors
  // UCesiumRuntimeSettings::EnableExperimentalOcclusionCullingFeature so that
  // it can be used as an EditCondition.
  UPROPERTY(Transient, VisibleDefaultsOnly, Category = "Cesium|Tile Occlusion")
  bool CanEnableOcclusionCulling = false;

  /**
   * Whether to cull tiles that are occluded.
   *
   * If this option is disabled, check that "Enable Experimental Occlusion
   * Culling Feature" is enabled in the Plugins -> Cesium section of the Project
   * Settings.
   *
   * When enabled, this feature will use Unreal's occlusion system to determine
   * if tiles are actually visible on the screen. For tiles found to be
   * occluded, the tile will not refine to show descendants, but it will still
   * be rendered to avoid holes. This results in less tile loads and less GPU
   * resource usage for dense, high-occlusion scenes like ground-level views in
   * cities.
   *
   * This will not work for tilesets with poorly fit bounding volumes and cause
   * more draw calls with very few extra culled tiles. When there is minimal
   * occlusion in a scene, such as with terrain tilesets and applications
   * focused on top-down views, this feature will yield minimal benefit and
   * potentially cause needless overhead.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetEnableOcclusionCulling,
      BlueprintSetter = SetEnableOcclusionCulling,
      Category = "Cesium|Tile Occlusion",
      meta = (EditCondition = "CanEnableOcclusionCulling"))
  bool EnableOcclusionCulling = true;

  /**
   * The number of CesiumBoundingVolumeComponents to use for querying the
   * occlusion state of traversed tiles.
   *
   * Only applicable when EnableOcclusionCulling is enabled.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetOcclusionPoolSize,
      BlueprintSetter = SetOcclusionPoolSize,
      Category = "Cesium|Tile Occlusion",
      meta =
          (EditCondition =
               "EnableOcclusionCulling && CanEnableOcclusionCulling",
           ClampMin = "0",
           ClampMax = "1000"))
  int32 OcclusionPoolSize = 500;

  /**
   * Whether to wait for valid occlusion results before refining tiles.
   *
   * Only applicable when EnableOcclusionCulling is enabled. When this option
   * is enabled, there may be small delays before tiles are refined, but there
   * may be an overall performance advantage by avoiding loads of descendants
   * that will be found to be occluded.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetDelayRefinementForOcclusion,
      BlueprintSetter = SetDelayRefinementForOcclusion,
      Category = "Cesium|Tile Occlusion",
      meta =
          (EditCondition =
               "EnableOcclusionCulling && CanEnableOcclusionCulling"))
  bool DelayRefinementForOcclusion = true;

  /**
   * Refreshes this tileset, ensuring that all materials and other settings are
   * applied. It is not usually necessary to invoke this, but when
   * behind-the-scenes changes are made and not reflected in the tileset, this
   * function can help.
   */
  UFUNCTION(CallInEditor, BlueprintCallable, Category = "Cesium")
  void RefreshTileset();

  /**
   * Pauses level-of-detail and culling updates of this tileset.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cesium|Debug")
  bool SuspendUpdate;

  /**
   * If true, this tileset is ticked/updated in the editor. If false, is only
   * ticked while playing (including Play-in-Editor).
   */
  UPROPERTY(EditAnywhere, Category = "Cesium|Debug")
  bool UpdateInEditor = true;

  /**
   * If true, stats about tile selection are printed to the Output Log.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium|Debug")
  bool LogSelectionStats = false;

  /**
   * If true, logs stats on the assets in this tileset's shared asset system to
   * the Output Log.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium|Debug")
  bool LogSharedAssetStats = false;

  /**
   * If true, draws debug text above each tile being rendered with information
   * about that tile.
   */
  UPROPERTY(EditAnywhere, Category = "Cesium|Debug")
  bool DrawTileInfo = false;

  /**
   * Define the collision profile for all the 3D tiles created inside this
   * actor.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadOnly,
      Category = "Collision",
      meta = (ShowOnlyInnerProperties, SkipUCSModifiedProperties))
  FBodyInstance BodyInstance;

  /**
   * A delegate that will be called whenever the tileset is fully loaded.
   */
  UPROPERTY(BlueprintAssignable, Category = "Cesium");
  FCompletedLoadTrigger OnTilesetLoaded;

  /**
   * Use a dithering effect when transitioning between tiles of different LODs.
   *
   * When this is set to true, Frustrum Culling and Fog Culling are always
   * disabled.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetUseLodTransitions,
      BlueprintSetter = SetUseLodTransitions,
      Category = "Cesium|Rendering")
  bool UseLodTransitions = false;

  /**
   * How long dithered LOD transitions between different tiles should take, in
   * seconds.
   *
   * Only relevant if UseLodTransitions is true.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Cesium|Rendering",
      meta = (EditCondition = "UseLodTransitions", EditConditionHides))
  float LodTransitionLength = 0.5f;

private:
  UPROPERTY(BlueprintGetter = GetLoadProgress, Category = "Cesium")
  float LoadProgress = 0.0f;

  /**
   * The type of source from which to load this tileset.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetTilesetSource,
      BlueprintSetter = SetTilesetSource,
      Category = "Cesium",
      meta = (DisplayName = "Source"))
  ETilesetSource TilesetSource = ETilesetSource::FromCesiumIon;

  /**
   * The URL of this tileset's "tileset.json" file.
   *
   * If this property is specified, the ion asset ID and token are ignored.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetUrl,
      BlueprintSetter = SetUrl,
      Category = "Cesium",
      meta = (EditCondition = "TilesetSource==ETilesetSource::FromUrl"))
  FString Url = "";

  /**
   * The ID of the Cesium ion asset to use.
   *
   * This property is ignored if the Url is specified.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetIonAssetID,
      BlueprintSetter = SetIonAssetID,
      Category = "Cesium",
      meta =
          (EditCondition = "TilesetSource==ETilesetSource::FromCesiumIon",
           ClampMin = 0))
  int64 IonAssetID;

  /**
   * The access token to use to access the Cesium ion resource.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetIonAccessToken,
      BlueprintSetter = SetIonAccessToken,
      Category = "Cesium",
      meta = (EditCondition = "TilesetSource==ETilesetSource::FromCesiumIon"))
  FString IonAccessToken;

  UPROPERTY(
      meta =
          (DeprecatedProperty,
           DeprecationMessage = "Use CesiumIonServer instead."))
  FString IonAssetEndpointUrl_DEPRECATED;

  /**
   * The Cesium ion Server from which this tileset is loaded.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetCesiumIonServer,
      BlueprintSetter = SetCesiumIonServer,
      Category = "Cesium",
      AdvancedDisplay,
      meta = (EditCondition = "TilesetSource==ETilesetSource::FromCesiumIon"))
  UCesiumIonServer* CesiumIonServer;

  /**
   * Headers to be attached to each request made for this tileset.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetRequestHeaders,
      BlueprintSetter = SetRequestHeaders,
      Category = "Cesium")
  TMap<FString, FString> RequestHeaders;

  /**
   * Check if the Cesium ion token used to access this tileset is working
   * correctly, and fix it if necessary.
   */
  UFUNCTION(CallInEditor, Category = "Cesium")
  void TroubleshootToken();

  /**
   * Whether to generate physics meshes for this tileset.
   *
   * Disabling this option will improve the performance of tile loading, but it
   * will no longer be possible to collide with the tileset since the physics
   * meshes will not be created.
   *
   * Physics meshes cannot be generated for primitives containing points.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetCreatePhysicsMeshes,
      BlueprintSetter = SetCreatePhysicsMeshes,
      Category = "Cesium|Physics")
  bool CreatePhysicsMeshes = true;

  /**
   * Whether to generate navigation collisions for this tileset.
   *
   * Enabling this option creates collisions for navigation when a 3D Tiles
   * tileset is loaded. It is recommended to set "Runtime Generation" to
   * "Static" in the navigation mesh settings in the project settings, as
   * collision calculations become very slow.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetCreateNavCollision,
      BlueprintSetter = SetCreateNavCollision,
      Category = "Cesium|Navigation")
  bool CreateNavCollision = false;

  /**
   * Whether to always generate a correct tangent space basis for tiles that
   * don't have them.
   *
   * Normally, a per-vertex tangent space basis is only required for glTF models
   * with a normal map. However, a custom, user-supplied material may need a
   * tangent space basis for other purposes. When this property is set to true,
   * tiles lacking an explicit tangent vector will have one computed
   * automatically using the MikkTSpace algorithm. When this property is false,
   * load time will be improved by skipping the generation of the tangent
   * vector, but the tangent space basis will be unreliable.
   *
   * Note that a tileset with "Enable Water Mask" set will include tangents
   * for tiles containing water, regardless of the value of this property.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetAlwaysIncludeTangents,
      BlueprintSetter = SetAlwaysIncludeTangents,
      Category = "Cesium|Rendering")
  bool AlwaysIncludeTangents = false;

  /**
   * Whether to generate smooth normals when normals are missing in the glTF.
   *
   * According to the Gltf spec: "When normals are not specified, client
   * implementations should calculate flat normals." However, calculating flat
   * normals requires duplicating vertices. This option allows the gltfs to be
   * sent with explicit smooth normals when the original gltf was missing
   * normals.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetGenerateSmoothNormals,
      BlueprintSetter = SetGenerateSmoothNormals,
      Category = "Cesium|Rendering")
  bool GenerateSmoothNormals = false;

  /**
   * Whether to request and render the water mask.
   *
   * Currently only applicable for quantized-mesh tilesets that support the
   * water mask extension.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetEnableWaterMask,
      BlueprintSetter = SetEnableWaterMask,
      Category = "Cesium|Rendering")
  bool EnableWaterMask = false;

  /**
   * Whether to ignore the KHR_materials_unlit extension on the glTF tiles in
   * this tileset, if it exists, and instead render with standard lighting and
   * shadows. This property will have no effect if the tileset does not have any
   * tiles that use this extension.
   *
   * The KHR_materials_unlit extension is often applied to photogrammetry
   * tilesets because lighting and shadows are already baked into their
   * textures.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetIgnoreKhrMaterialsUnlit,
      BlueprintSetter = SetIgnoreKhrMaterialsUnlit,
      Category = "Cesium|Rendering",
      meta = (DisplayName = "Ignore KHR_materials_unlit"))
  bool IgnoreKhrMaterialsUnlit = false;

  /**
   * A custom Material to use to render opaque elements in this tileset, in
   * order to implement custom visual effects.
   *
   * The custom material should generally be created by copying the Material
   * Instance "MI_CesiumThreeOverlaysAndClipping" and customizing the copy as
   * desired.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetMaterial,
      BlueprintSetter = SetMaterial,
      Category = "Cesium|Rendering")
  UMaterialInterface* Material = nullptr;

  /**
   * A custom Material to use to render translucent elements of the tileset, in
   * order to implement custom visual effects.
   *
   * The custom material should generally be created by copying the Material
   * Instance "MI_CesiumThreeOverlaysAndClippingTranslucent" and customizing the
   * copy as desired. Make sure that its Material Property Overrides -> Blend
   * Mode is set to "Translucent".
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetTranslucentMaterial,
      BlueprintSetter = SetTranslucentMaterial,
      Category = "Cesium|Rendering")
  UMaterialInterface* TranslucentMaterial = nullptr;

  /**
   * A custom Material to use to render this tileset in areas where the
   * watermask is, in order to implement custom visual effects.
   * Currently only applicable for quantized-mesh tilesets that support the
   * water mask extension.
   *
   * The custom material should generally be created by copying the Material
   * Instance "MI_CesiumThreeOverlaysAndClippingAndWater" and customizing the
   * copy as desired.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetWaterMaterial,
      BlueprintSetter = SetWaterMaterial,
      Category = "Cesium|Rendering")
  UMaterialInterface* WaterMaterial = nullptr;

  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetCustomDepthParameters,
      BlueprintSetter = SetCustomDepthParameters,
      Category = "Rendering",
      meta = (ShowOnlyInnerProperties))
  FCustomDepthParameters CustomDepthParameters;

  /**
   * If this tileset contains points, their appearance can be configured with
   * these point cloud shading parameters.
   *
   * These settings are not supported on mobile platforms.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetPointCloudShading,
      BlueprintSetter = SetPointCloudShading,
      Category = "Cesium|Rendering")
  FCesiumPointCloudShading PointCloudShading;

  /**
   * Array of runtime virtual textures into which we draw the mesh for this
   * actor. The material also needs to be set up to output to a virtual texture.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetRuntimeVirtualTextures,
      BlueprintSetter = SetRuntimeVirtualTextures,
      Category = "VirtualTexture",
      meta = (DisplayName = "Draw in Virtual Textures"))
  TArray<TObjectPtr<URuntimeVirtualTexture>> RuntimeVirtualTextures;

  /** Controls if this component draws in the main pass as well as in the
   * virtual texture. You must refresh the Tileset after changing this value! */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetVirtualTextureRenderPassType,
      Category = VirtualTexture,
      meta = (DisplayName = "Draw in Main Pass"))
  ERuntimeVirtualTextureMainPassType VirtualTextureRenderPassType =
      ERuntimeVirtualTextureMainPassType::Exclusive;

  /**
   * Translucent objects with a lower sort priority draw behind objects with a
   * higher priority. Translucent objects with the same priority are rendered
   * from back-to-front based on their bounds origin. This setting is also used
   * to sort objects being drawn into a runtime virtual texture.
   *
   * Ignored if the object is not translucent.  The default priority is zero.
   * Warning: This should never be set to a non-default value unless you know
   * what you are doing, as it will prevent the renderer from sorting correctly.
   * It is especially problematic on dynamic gameplay effects.
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintGetter = GetTranslucencySortPriority,
      BlueprintSetter = SetTranslucencySortPriority,
      AdvancedDisplay,
      Category = Rendering)
  int32 TranslucencySortPriority;

protected:
  UPROPERTY()
  FString PlatformName;

public:
  UFUNCTION(BlueprintGetter, Category = "Cesium")
  float GetLoadProgress() const { return LoadProgress; }

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  bool GetUseLodTransitions() const { return UseLodTransitions; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetUseLodTransitions(bool InUseLodTransitions);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  ETilesetSource GetTilesetSource() const { return TilesetSource; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetTilesetSource(ETilesetSource InSource);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  FString GetUrl() const { return Url; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetUrl(const FString& InUrl);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  TMap<FString, FString> GetRequestHeaders() const { return RequestHeaders; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetRequestHeaders(const TMap<FString, FString>& InRequestHeaders);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  int64 GetIonAssetID() const { return IonAssetID; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetIonAssetID(int64 InAssetID);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  FString GetIonAccessToken() const { return IonAccessToken; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetIonAccessToken(const FString& InAccessToken);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  UCesiumIonServer* GetCesiumIonServer() const { return CesiumIonServer; }

  UFUNCTION(BlueprintGetter, Category = "VirtualTexture")
  TArray<URuntimeVirtualTexture*> GetRuntimeVirtualTextures() const {
    return RuntimeVirtualTextures;
  }

  UFUNCTION(BlueprintSetter, Category = "VirtualTexture")
  void SetRuntimeVirtualTextures(
      TArray<URuntimeVirtualTexture*> InRuntimeVirtualTextures);

  UFUNCTION(BlueprintGetter, Category = "VirtualTexture")
  ERuntimeVirtualTextureMainPassType GetVirtualTextureRenderPassType() const {
    return VirtualTextureRenderPassType;
  }

  UFUNCTION(BlueprintGetter, Category = Rendering)
  int32 GetTranslucencySortPriority() { return TranslucencySortPriority; }

  UFUNCTION(BlueprintSetter, Category = Rendering)
  void SetTranslucencySortPriority(int32 InTranslucencySortPriority);

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetCesiumIonServer(UCesiumIonServer* Server);

  UFUNCTION(BlueprintGetter, Category = "Cesium")
  double GetMaximumScreenSpaceError() { return MaximumScreenSpaceError; }

  UFUNCTION(BlueprintSetter, Category = "Cesium")
  void SetMaximumScreenSpaceError(double InMaximumScreenSpaceError);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Tile Culling|Experimental")
  bool GetEnableOcclusionCulling() const;

  UFUNCTION(BlueprintSetter, Category = "Cesium|Tile Culling|Experimental")
  void SetEnableOcclusionCulling(bool bEnableOcclusionCulling);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Tile Culling|Experimental")
  int32 GetOcclusionPoolSize() const { return OcclusionPoolSize; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Tile Culling|Experimental")
  void SetOcclusionPoolSize(int32 newOcclusionPoolSize);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Tile Culling|Experimental")
  bool GetDelayRefinementForOcclusion() const {
    return DelayRefinementForOcclusion;
  }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Tile Culling|Experimental")
  void SetDelayRefinementForOcclusion(bool bDelayRefinementForOcclusion);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Physics")
  bool GetCreatePhysicsMeshes() const { return CreatePhysicsMeshes; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Physics")
  void SetCreatePhysicsMeshes(bool bCreatePhysicsMeshes);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Navigation")
  bool GetCreateNavCollision() const { return CreateNavCollision; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Navigation")
  void SetCreateNavCollision(bool bCreateNavCollision);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  bool GetAlwaysIncludeTangents() const { return AlwaysIncludeTangents; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetAlwaysIncludeTangents(bool bAlwaysIncludeTangents);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  bool GetGenerateSmoothNormals() const { return GenerateSmoothNormals; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetGenerateSmoothNormals(bool bGenerateSmoothNormals);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  bool GetEnableWaterMask() const { return EnableWaterMask; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetEnableWaterMask(bool bEnableMask);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  bool GetIgnoreKhrMaterialsUnlit() const { return IgnoreKhrMaterialsUnlit; }
  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetIgnoreKhrMaterialsUnlit(bool bIgnoreKhrMaterialsUnlit);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  UMaterialInterface* GetMaterial() const { return Material; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetMaterial(UMaterialInterface* InMaterial);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  UMaterialInterface* GetTranslucentMaterial() const {
    return TranslucentMaterial;
  }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetTranslucentMaterial(UMaterialInterface* InMaterial);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  UMaterialInterface* GetWaterMaterial() const { return WaterMaterial; }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetWaterMaterial(UMaterialInterface* InMaterial);

  UFUNCTION(BlueprintGetter, Category = "Rendering")
  FCustomDepthParameters GetCustomDepthParameters() const {
    return CustomDepthParameters;
  }

  UFUNCTION(BlueprintSetter, Category = "Rendering")
  void SetCustomDepthParameters(FCustomDepthParameters InCustomDepthParameters);

  UFUNCTION(BlueprintGetter, Category = "Cesium|Rendering")
  FCesiumPointCloudShading GetPointCloudShading() const {
    return PointCloudShading;
  }

  UFUNCTION(BlueprintSetter, Category = "Cesium|Rendering")
  void SetPointCloudShading(FCesiumPointCloudShading InPointCloudShading);

  UFUNCTION(BlueprintCallable, Category = "Cesium|Rendering")
  void PlayMovieSequencer();

  UFUNCTION(BlueprintCallable, Category = "Cesium|Rendering")
  void StopMovieSequencer();

  UFUNCTION(BlueprintCallable, Category = "Cesium|Rendering")
  void PauseMovieSequencer();

  /**
   * This method is not supposed to be called by clients. It is currently
   * only required by the UnrealPrepareRendererResources.
   *
   * @internal
   * See {@link
   * UCesium3DTilesetRoot::GetCesiumTilesetToUnrealRelativeWorldTransform}.
   * @endinternal
   */
  const glm::dmat4& GetCesiumTilesetToUnrealRelativeWorldTransform() const;

  Cesium3DTilesSelection::Tileset* GetTileset() {
    return this->_pTileset.Get();
  }
  const Cesium3DTilesSelection::Tileset* GetTileset() const {
    return this->_pTileset.Get();
  }

  const std::optional<FCesiumFeaturesMetadataDescription>&
  getFeaturesMetadataDescription() const {
    return this->_featuresMetadataDescription;
  }

  // AActor overrides (some or most of them should be protected)
  virtual bool ShouldTickIfViewportsOnly() const override;
  virtual void Tick(float DeltaTime) override;
  virtual void BeginDestroy() override;
  virtual bool IsReadyForFinishDestroy() override;
  virtual void Destroyed() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void PostLoad() override;
  virtual void Serialize(FArchive& Ar) override;

  void UpdateLoadStatus();

  // UObject overrides
#if WITH_EDITOR
  virtual void
  PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
  virtual void PostEditChangeChainProperty(
      FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
  virtual void PostEditUndo() override;
  virtual void PostEditImport() override;
  virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;
  virtual void OnConstruction(const FTransform& Transform) override;

  /**
   * Called after the C++ constructor and after the properties have
   * been initialized, including those loaded from config.
   */
  void PostInitProperties() override;

  virtual void NotifyHit(
      class UPrimitiveComponent* MyComp,
      AActor* Other,
      class UPrimitiveComponent* OtherComp,
      bool bSelfMoved,
      FVector HitLocation,
      FVector HitNormal,
      FVector NormalImpulse,
      const FHitResult& Hit) override;

private:
  void LoadTileset();
  void DestroyTileset();

  static Cesium3DTilesSelection::ViewState CreateViewStateFromViewParameters(
      const FCesiumCamera& camera,
      const glm::dmat4& unrealWorldToTileset,
      UCesiumEllipsoid* ellipsoid);

  std::vector<FCesiumCamera> GetCameras() const;
  std::vector<FCesiumCamera> GetPlayerCameras() const;
  std::vector<FCesiumCamera> GetSceneCaptures() const;

public:
  /**
   * Update the transforms of the glTF components based on the
   * the transform of the root component.
   *
   * This is supposed to be called during Tick, if the transform of
   * the root component has changed since the previous Tick.
   */
  void UpdateTransformFromCesium();

private:
  /**
   * The event handler for ACesiumGeoreference::OnEllipsoidChanged.
   */
  UFUNCTION(CallInEditor)
  void HandleOnGeoreferenceEllipsoidChanged(
      UCesiumEllipsoid* OldEllipsoid,
      UCesiumEllipsoid* NewEllpisoid);

  /**
   * Writes the values of all properties of this actor into the
   * TilesetOptions, to take them into account during the next
   * traversal.
   */
  void updateTilesetOptionsFromProperties();

  /**
   * Update all the "_last..." fields of this instance based
   * on the given ViewUpdateResult, printing a log message
   * if any value changed.
   *
   * @param result The ViewUpdateREsult
   */
  void updateLastViewUpdateResultState(
      const Cesium3DTilesSelection::ViewUpdateResult& result);

  /**
   * Creates the visual representations of the given tiles to
   * be rendered in the current frame.
   *
   * @param tiles The tiles
   */
  void showTilesToRender(const std::vector<CesiumUtility::IntrusivePointer<
                             Cesium3DTilesSelection::Tile>>& tiles);

  /**
   * Will be called after the tileset is loaded or spawned, to register
   * a delegate that calls OnFocusEditorViewportOnThis when this
   * tileset is double-clicked
   */
  void AddFocusViewportDelegate();

#if WITH_EDITOR
  std::vector<FCesiumCamera> GetEditorCameras() const;

  /**
   * Will focus all viewports on this tileset.
   *
   * This is called when double-clicking the tileset in the World Outliner.
   * It will move the tileset into the center of the view, *even if* the
   * tileset was not visible before, and no geometry has been created yet
   * for the tileset: It solely operates on the tile bounding volume that
   * was given in the root tile.
   */
  void OnFocusEditorViewportOnThis();

  void RuntimeSettingsChanged(
      UObject* pObject,
      struct FPropertyChangedEvent& changed);
#endif

private:
  TUniquePtr<Cesium3DTilesSelection::Tileset> _pTileset;

#ifdef CESIUM_DEBUG_TILE_STATES
  TUniquePtr<Cesium3DTilesSelection::DebugTileStateDatabase> _pStateDebug;
#endif

  std::optional<FCesiumFeaturesMetadataDescription>
      _featuresMetadataDescription;

  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  std::optional<FMetadataDescription> _metadataDescription_DEPRECATED;
  PRAGMA_ENABLE_DEPRECATION_WARNINGS

  // For debug output
  uint32_t _lastTilesRendered;
  uint32_t _lastWorkerThreadTileLoadQueueLength;
  uint32_t _lastMainThreadTileLoadQueueLength;

  uint32_t _lastTilesVisited;
  uint32_t _lastCulledTilesVisited;
  uint32_t _lastTilesCulled;
  uint32_t _lastTilesOccluded;
  uint32_t _lastTilesWaitingForOcclusionResults;
  uint32_t _lastMaxDepthVisited;

  std::chrono::high_resolution_clock::time_point _startTime;

  bool _captureMovieMode;
  bool _beforeMoviePreloadAncestors;
  bool _beforeMoviePreloadSiblings;
  int32_t _beforeMovieLoadingDescendantLimit;
  bool _beforeMovieUseLodTransitions;

  bool _scaleUsingDPI;

  // This is used as a workaround for cesium-native#186
  //
  // The tiles that are no longer supposed to be rendered in the current
  // frame, according to ViewUpdateResult::tilesToHideThisFrame,
  // are kept in this list, and hidden in the NEXT frame, because some
  // internal occlusion culling information from Unreal might prevent
  // the tiles that are supposed to be rendered instead from appearing
  // immediately.
  //
  // If we find a way to clear the wrong occlusion information in the
  // Unreal Engine, then this field may be removed, and the
  // tilesToHideThisFrame may be hidden immediately.
  std::vector<CesiumUtility::IntrusivePointer<Cesium3DTilesSelection::Tile>>
      _tilesToHideNextFrame;

  int32 _tilesetsBeingDestroyed;

  friend class UnrealPrepareRendererResources;
  friend class UCesiumGltfPointsComponent;
};
