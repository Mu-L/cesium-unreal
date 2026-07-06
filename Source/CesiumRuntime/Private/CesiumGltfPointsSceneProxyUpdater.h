// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "Cesium3DTileset.h"
#include "CesiumGltfPointsComponent.h"
#include "CesiumGltfPointsSceneProxy.h"

/**
 * @brief Propagates settings from ACesium3DTileset to any
 * UCesiumGltfPointsComponent's that it parents.
 */
class FCesiumGltfPointsSceneProxyUpdater {
public:
  /**
   * @brief Updates proxies with new tileset settings. Must be called from a
   * game thread. */
  static void UpdateSettingsInProxies(ACesium3DTileset* pTileset) {
    if (!IsValid(pTileset) || !IsInGameThread()) {
      return;
    }

    TInlineComponentArray<UCesiumGltfPointsComponent*> componentArray;
    pTileset->GetComponents<UCesiumGltfPointsComponent>(componentArray);

    // Used to pass tileset data updates to render thread
    TArray<FCesiumGltfPointsSceneProxy*> sceneProxies;
    TArray<FCesiumGltfPointsSceneProxyTilesetData> proxyTilesetData;

    for (UCesiumGltfPointsComponent* pPointsComponent : componentArray) {
      FCesiumGltfPointsSceneProxy* pPointsProxy =
          static_cast<FCesiumGltfPointsSceneProxy*>(
              pPointsComponent->SceneProxy);
      if (pPointsProxy) {
        sceneProxies.Add(pPointsProxy);
      }

      FCesiumGltfPointsSceneProxyTilesetData tilesetData;
      tilesetData.updateFromComponent(pPointsComponent);
      proxyTilesetData.Add(tilesetData);
    }

    // Update tileset data
    ENQUEUE_RENDER_COMMAND(TransferCesium3DTilesetSettingsToPointsProxies)
    ([sceneProxies,
      proxyTilesetData](FRHICommandListImmediate& RHICmdList) mutable {
      // Iterate over proxies and update their data
      for (int32 i = 0; i < sceneProxies.Num(); i++) {
        sceneProxies[i]->UpdateTilesetData(proxyTilesetData[i]);
      }
    });
  }
};
