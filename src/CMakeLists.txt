cmake_minimum_required(VERSION 3.24)

project(BFVR LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "BFVR's native observer is Windows-only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BFVR_PRESENTER_ONLY "Build only the x64 OpenXR companion presenter." OFF)

set(BFVR_OPENXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openxr-1.1.61")
set(BFVR_OPENXR_INCLUDE_DIRECTORY "${BFVR_OPENXR_ROOT}/include")
set(BFVR_MINHOOK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/minhook-1.3.4")
set(BFVR_D3D8TO9_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/d3d8to9-1.15.1")

if(NOT EXISTS "${BFVR_OPENXR_INCLUDE_DIRECTORY}/openxr/openxr.h")
  message(FATAL_ERROR "The pinned OpenXR 1.1.61 headers are missing from BFVR/third_party.")
endif()

if(BFVR_PRESENTER_ONLY)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "BFVR_PRESENTER_ONLY requires an x64 generator.")
  endif()

  set(BFVR_OPENXR_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/openxr/win64/openxr_loader.dll")
  if(NOT EXISTS "${BFVR_OPENXR_LOADER}")
    message(FATAL_ERROR "The pinned x64 OpenXR loader is missing from BFVR/runtime.")
  endif()

  add_executable(BFVRPresenter
    presenter/BFVRPresenter.cpp
    presenter/DesktopMirror.cpp
    presenter/D3D11TextureScaler.cpp
    presenter/SharedControlChannel.cpp
    presenter/SharedTextureConsumer.cpp
    openxr/OpenXRPresentation.cpp
  )
  add_executable(BFVRSharedTextureConsumerProbe
    presenter/BFVRSharedTextureConsumerProbe.cpp
    presenter/D3D11TextureScaler.cpp
    presenter/SharedControlChannel.cpp
    presenter/SharedTextureConsumer.cpp
  )
  foreach(target BFVRPresenter BFVRSharedTextureConsumerProbe)
    target_compile_definitions(${target} PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${BFVR_OPENXR_INCLUDE_DIRECTORY}")
    target_link_libraries(${target} PRIVATE
      kernel32 ole32 d3d11 dxgi d3dcompiler)
    set_property(TARGET ${target} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
  endforeach()
  add_custom_command(TARGET BFVRPresenter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:BFVRPresenter>/runtime/openxr/win64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${BFVR_OPENXR_LOADER}"
      "$<TARGET_FILE_DIR:BFVRPresenter>/runtime/openxr/win64/openxr_loader.dll")
  return()
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The BFVR client, loader, and producer probe require a Win32 generator.")
endif()

set(BFVR_OPENXR_LOADER "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/openxr/win32/openxr_loader.dll")
if(NOT EXISTS "${BFVR_OPENXR_LOADER}")
  message(FATAL_ERROR "The pinned x86 OpenXR loader is missing from BFVR/runtime.")
endif()

if(NOT EXISTS "${BFVR_MINHOOK_ROOT}/include/MinHook.h")
  message(FATAL_ERROR "The pinned MinHook 1.3.4 source is missing from BFVR/third_party.")
endif()

if(NOT EXISTS "${BFVR_D3D8TO9_ROOT}/source/d3d8to9.cpp")
  message(FATAL_ERROR "The pinned d3d8to9 1.15.1 source is missing from BFVR/third_party.")
endif()

# MinHook is compiled into BFVRClient only for explicit bounded observer and
# stereo-proof hooks. It never supplies a proxy D3D8 DLL or a runtime
# dependency.
add_subdirectory("${BFVR_MINHOOK_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/minhook")
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_subdirectory(
  "${BFVR_D3D8TO9_ROOT}"
  "${CMAKE_CURRENT_BINARY_DIR}/d3d8to9"
  EXCLUDE_FROM_ALL)
set_property(TARGET d3d8to9 PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The continuous BFVR path must not emit d3d8to9's per-call debug log. BFVR's
# observer log records the bounded compatibility evidence instead.
target_compile_definitions(d3d8to9 PRIVATE D3D8TO9NOLOG)

add_library(BFVRClient SHARED
  client/BFVRClient.cpp
  client/D3D8CallInventory.cpp
  client/ControllerInputCache.cpp
  client/ControllerInputOverlay.cpp
  client/CrosshairOverlay.cpp
  client/MenuPointerOverlay.cpp
  client/StartupMenuPresentation.cpp
  client/D3D8ImportRoute.cpp
  client/D3D8PresentationConfiguration.cpp
  client/D3D8RenderViewPoseHook.cpp
  client/BFSoldierVrMotionFilter.cpp
  client/BFSoldierFirstPersonArmProbe.cpp
  client/BFSoldierLeftGripRotationBinding.cpp
  client/BFSoldierNativeArmIk.cpp
  client/BFSoldierOffHandSupportBinding.cpp
  client/BFSoldierOffHandWeaponSteering.cpp
  client/BFSoldierTrackedHandPose.cpp
  client/D3D8RuntimePosePolicy.cpp
  client/D3D8RuntimeRedirect.cpp
  client/BFSoldierBoneResolver.cpp
  client/PlayerInputProbe.cpp
  client/WeaponFireProbe.cpp
  client/WeaponAimOverlay.cpp
  client/WeaponPoseRuntimeCache.cpp
  client/D3D8SharedPresentationBridge.cpp
  client/D3D8StateCensus.cpp
  client/D3D8WeaponTransformOwnershipProbe.cpp
  client/D3D8WeaponMotionOverlay.cpp
  client/D3D8WeaponViewModelProbe.cpp
  client/D3D8To9SharedTextureProducer.cpp
  client/D3D8To9VertexShaderIdentity.cpp
  client/D3D8StereoFrameTransfer.cpp
  client/D3D8StereoPairProbe.cpp
  client/D3D8StereoReadback.cpp
  client/D3D8SpriteShaderTransform.cpp
  client/D3D8TreeSpriteShaderTransform.cpp
  client/D3D8StereoShaderTransform.cpp
  client/D3D8StereoProbeRecords.cpp
  client/D3D8StereoProbeReporting.cpp
  client/internal/BFVRClientViewDiagnostics.inl
  client/internal/BFVRClientFrameDiagnostics.inl
  client/internal/BFVRClientSurfaceDiagnostics.inl
  client/internal/BFVRClientEngineDiagnostics.inl
  openxr/OpenXRBootstrap.cpp
  presenter/SharedControlChannel.cpp
  presenter/D3DSystemRuntime.cpp
  presenter/SharedTextureProducer.cpp
)

add_library(BFVRD3D8To9FlatClient SHARED
  client/BFVRD3D8To9FlatClient.cpp
  client/D3D8ImportRoute.cpp
  client/D3D8RuntimeRedirect.cpp
)

add_library(BFVRStereoMath STATIC
  stereo/D3D8DrawPolicy.cpp
  stereo/D3D8FirstPersonArmPolicy.cpp
  stereo/D3D8FrameCompositionPolicy.cpp
  stereo/D3D8SemanticDrawPolicy.cpp
  stereo/D3D8ShaderTransform.cpp
  stereo/D3D8WeaponDrawPolicy.cpp
  stereo/OffHandSupportPolicy.cpp
  stereo/OffHandWeaponSteeringMath.cpp
  stereo/WeaponMotionPolicy.cpp
  stereo/WeaponFireAimMath.cpp
  stereo/StereoMath.cpp
  stereo/TreeAngleSlicePolicy.cpp
  stereo/UiPointerMath.cpp
  stereo/WeaponPoseMath.cpp
)

add_executable(BFVRLoader
  loader/BFVRLoader.cpp
)

add_executable(BFVROpenXRProbe
  openxr/BFVROpenXRProbe.cpp
  openxr/OpenXRBootstrap.cpp
  openxr/OpenXRPresentation.cpp
)

add_executable(BFVRSharedTextureProducerProbe
  presenter/BFVRSharedTextureProducerProbe.cpp
  presenter/SharedControlChannel.cpp
  presenter/D3DSystemRuntime.cpp
  presenter/SharedTextureProducer.cpp
)

add_executable(BFVRD3D8To9SharedSurfaceProbe
  presenter/BFVRD3D8To9SharedSurfaceProbe.cpp
  presenter/D3D8To9CrossProcessProbe.cpp
  presenter/D3DSystemRuntime.cpp
  presenter/SharedControlChannel.cpp
)

add_executable(BFVRStereoMathTests
  client/D3D8RuntimePosePolicy.cpp
  client/D3D8SpriteShaderTransform.cpp
  client/D3D8TreeSpriteShaderTransform.cpp
  client/D3D8StereoShaderTransform.cpp
  client/D3D8StereoProbeRecords.cpp
  stereo/StereoMathTests.cpp
)

add_executable(BFVRWeaponFireAimMathTests
  stereo/WeaponFireAimMathTests.cpp
)

add_executable(BFVROffHandSupportPolicyTests
  client/BFSoldierOffHandSupportBinding.cpp
  client/BFSoldierOffHandWeaponSteering.cpp
  client/BFSoldierTrackedHandPose.cpp
  stereo/OffHandSupportPolicyTests.cpp
)

add_executable(BFVRRfaInspect
  tools/RfaInspect.cpp
)

foreach(target BFVRClient BFVRD3D8To9FlatClient BFVRLoader BFVROpenXRProbe BFVRSharedTextureProducerProbe BFVRD3D8To9SharedSurfaceProbe BFVRStereoMath BFVRStereoMathTests BFVRWeaponFireAimMathTests BFVROffHandSupportPolicyTests BFVRRfaInspect)
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  endif()
endforeach()

target_link_libraries(BFVRClient PRIVATE kernel32 advapi32)
target_link_libraries(BFVRD3D8To9FlatClient PRIVATE kernel32)
target_link_libraries(BFVRLoader PRIVATE kernel32)
target_link_libraries(BFVROpenXRProbe PRIVATE kernel32 d3d11 dxgi)
target_link_libraries(BFVRSharedTextureProducerProbe PRIVATE kernel32)
target_link_libraries(BFVRD3D8To9SharedSurfaceProbe PRIVATE kernel32 user32)
target_include_directories(BFVRSharedTextureProducerProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRD3D8To9SharedSurfaceProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${BFVR_D3D8TO9_ROOT}/source")

target_link_libraries(BFVRClient PRIVATE minhook BFVRStereoMath)
target_include_directories(BFVRD3D8To9FlatClient PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRStereoMath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(BFVRStereoMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRWeaponFireAimMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVROffHandSupportPolicyTests PRIVATE BFVRStereoMath)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME BFVRStereoMathTests COMMAND BFVRStereoMathTests)
  add_test(
    NAME BFVRWeaponFireAimMathTests
    COMMAND BFVRWeaponFireAimMathTests)
  add_test(
    NAME BFVROffHandSupportPolicyTests
    COMMAND BFVROffHandSupportPolicyTests)
endif()

foreach(target BFVRClient BFVROpenXRProbe)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${BFVR_OPENXR_INCLUDE_DIRECTORY}"
  )
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/runtime/openxr/win32"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${BFVR_OPENXR_LOADER}"
      "$<TARGET_FILE_DIR:${target}>/runtime/openxr/win32/openxr_loader.dll"
  )
endforeach()

target_include_directories(BFVRClient PRIVATE
  "${BFVR_D3D8TO9_ROOT}/source")

set_target_properties(BFVRClient PROPERTIES OUTPUT_NAME "BFVRClient")
set_target_properties(BFVRD3D8To9FlatClient PROPERTIES
  OUTPUT_NAME "BFVRD3D8To9FlatClient")
set_target_properties(BFVRLoader PROPERTIES OUTPUT_NAME "BFVRLoader")
set_target_properties(BFVROpenXRProbe PROPERTIES OUTPUT_NAME "BFVROpenXRProbe")
set_target_properties(BFVRSharedTextureProducerProbe PROPERTIES
  OUTPUT_NAME "BFVRSharedTextureProducerProbe")
set_target_properties(BFVRD3D8To9SharedSurfaceProbe PROPERTIES
  OUTPUT_NAME "BFVRD3D8To9SharedSurfaceProbe")

add_custom_target(BFVRD3D8To9RuntimeCopy ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:d3d8to9>"
    "$<TARGET_FILE_DIR:BFVRClient>/BFVRD3D8To9.dll"
  DEPENDS d3d8to9)
foreach(target BFVRClient BFVRD3D8To9FlatClient BFVRD3D8To9SharedSurfaceProbe)
  add_dependencies(${target} BFVRD3D8To9RuntimeCopy)
endforeach()

# These native targets are intentionally just an observation prototype.  The
# release launcher/client layout is not produced until the passive trace has
# been validated on a reproducible local gameplay baseline.
