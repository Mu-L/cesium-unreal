// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#if WITH_EDITOR

#include <functional>
#include <swl/variant.hpp>

#include "CesiumSceneGeneration.h"

namespace Cesium {

struct TestPass {
  typedef swl::variant<int, float> TestingParameter;
  typedef std::function<void(SceneGenerationContext&, TestingParameter)>
      SetupCallback;
  typedef std::function<
      bool(SceneGenerationContext&, SceneGenerationContext&, TestingParameter)>
      VerifyCallback;

  FString name;
  SetupCallback setupStep;
  VerifyCallback verifyStep;
  TestingParameter optionalParameter;

  bool testInProgress = false;
  double startMark = 0;
  double endMark = 0;
  double elapsedTime = 0;

  bool isFastest = false;
};

typedef std::function<void(const std::vector<TestPass>&)> ReportCallback;

bool RunLoadTest(
    const FString& testName,
    std::function<void(SceneGenerationContext&)> locationSetup,
    const std::vector<TestPass>& testPasses,
    int viewportWidth,
    int viewportHeight,
    ReportCallback optionalReportStep = nullptr);

}; // namespace Cesium

#endif
