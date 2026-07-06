// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include "CesiumCompat.h"
#include "CesiumPointAttenuationVertexFactory.h"
#include "CesiumPointCloudShading.h"
#include "PrimitiveSceneProxy.h"
#include <glm/vec3.hpp>

class UCesiumGltfPointsComponent;

/**
 * Used to pass tile data and Cesium3DTileset settings to a SceneProxy, usually
 * via render thread.
 */
struct FCesiumGltfPointsSceneProxyTilesetData {
  FCesiumPointCloudShading pointCloudShading;
  double maximumScreenSpaceError;
  bool usesAdditiveRefinement;
  float geometricError;
  glm::vec3 dimensions;
  int32 diameter;

  FCesiumGltfPointsSceneProxyTilesetData();

  void updateFromComponent(UCesiumGltfPointsComponent* pComponent);
};

class FCesiumGltfPointsSceneProxy final : public FPrimitiveSceneProxy {
public:
  SIZE_T GetTypeHash() const override;

  FCesiumGltfPointsSceneProxy(
      UCesiumGltfPointsComponent* InComponent,
      FSceneInterfaceWrapper InSceneInterfaceParams);

  virtual ~FCesiumGltfPointsSceneProxy();

protected:
  virtual void
  CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;

  virtual void DestroyRenderThreadResources() override;

  virtual void GetDynamicMeshElements(
      const TArray<const FSceneView*>& Views,
      const FSceneViewFamily& ViewFamily,
      uint32 VisibilityMap,
      FMeshElementCollector& Collector) const override;

  virtual FPrimitiveViewRelevance
  GetViewRelevance(const FSceneView* View) const override;

  virtual uint32 GetMemoryFootprint(void) const override;

public:
  void UpdateTilesetData(
      const FCesiumGltfPointsSceneProxyTilesetData& InTilesetData);

private:
  void CreatePointAttenuationUserData(
      FMeshBatchElement& BatchElement,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;

  void CreateMeshWithAttenuation(
      FMeshBatch& Mesh,
      const FSceneView* View,
      FMeshElementCollector& Collector) const;
  void CreateMesh(FMeshBatch& Mesh) const;

  float getGeometricError() const;

  /**
   * @brief The original render data of the static mesh.
   */
  const FStaticMeshRenderData* _pRenderData;
  int32_t _numPoints;

  /**
   * @brief Whether or not the shader platform supports attenuation.
   */
  bool _attenuationSupported;

  /**
   * @brief Data from the UCesiumGltfPointsComponent that owns this scene proxy,
   * as well as its ACesium3DTileset.
   */
  FCesiumGltfPointsSceneProxyTilesetData _tilesetData;

  FCesiumPointAttenuationVertexFactory _attenuationVertexFactory;
  FCesiumPointAttenuationIndexBuffer _attenuationIndexBuffer;
  UMaterialInterface* _pMaterial;
  FMaterialRelevance _materialRelevance;
};
