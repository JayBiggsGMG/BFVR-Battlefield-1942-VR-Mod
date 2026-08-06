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
    client/MainMenuOverlay.cpp
    client/QuickMenuArt.cpp
    client/SettingsMenuArt.cpp
    presenter/BFVRPresenter.cpp
    presenter/DesktopMirror.cpp
    presenter/D3D11AmbientOcclusion.cpp
    presenter/D3D11MainMenuOverlay.cpp
    presenter/D3D11ScreenSpaceGlobalIllumination.cpp
    presenter/D3D11TextureScaler.cpp
    presenter/D3D11WaterReflection.cpp
    presenter/SharedControlChannel.cpp
    presenter/SharedTextureConsumer.cpp
    settings/UserSettings.cpp
    openxr/OpenXRQuickMenu.cpp
    openxr/OpenXRPresentation.cpp
    openxr/OpenXRPresentationSupport.cpp
    openxr/OpenXRScopeOverlayLayer.cpp
    openxr/OpenXRTrackingBasis.cpp
    stereo/QuickMenuInteraction.cpp
    stereo/QuickMenuMirrorMath.cpp
    stereo/SettingsMenuInteraction.cpp
    stereo/ScopeViewMath.cpp
    stereo/UiPointerMath.cpp
  )
  add_executable(BFVRSharedTextureConsumerProbe
    client/MainMenuOverlay.cpp
    presenter/BFVRSharedTextureConsumerProbe.cpp
    presenter/D3D11AmbientOcclusion.cpp
    presenter/D3D11MainMenuOverlay.cpp
    presenter/D3D11ScreenSpaceGlobalIllumination.cpp
    presenter/D3D11TextureScaler.cpp
    presenter/D3D11WaterReflection.cpp
    presenter/SharedControlChannel.cpp
    presenter/SharedTextureConsumer.cpp
    settings/UserSettings.cpp
  )
  add_executable(BFVRAmbientOcclusionGpuProbe
    presenter/BFVRAmbientOcclusionGpuProbe.cpp
    presenter/D3D11AmbientOcclusion.cpp
    presenter/D3D11TextureScaler.cpp
  )
  add_executable(BFVRBloomGpuProbe
    presenter/BFVRBloomGpuProbe.cpp
    presenter/D3D11TextureScaler.cpp
  )
  add_executable(BFVRScreenSpaceGlobalIlluminationGpuProbe
    presenter/BFVRScreenSpaceGlobalIlluminationGpuProbe.cpp
    presenter/D3D11ScreenSpaceGlobalIllumination.cpp
  )
  add_executable(BFVRWaterReflectionGpuProbe
    presenter/BFVRWaterReflectionGpuProbe.cpp
    presenter/D3D11WaterReflection.cpp
  )
  foreach(target BFVRPresenter BFVRSharedTextureConsumerProbe BFVRAmbientOcclusionGpuProbe BFVRBloomGpuProbe BFVRScreenSpaceGlobalIlluminationGpuProbe BFVRWaterReflectionGpuProbe)
    target_compile_definitions(${target} PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${BFVR_OPENXR_INCLUDE_DIRECTORY}")
    target_link_libraries(${target} PRIVATE
      kernel32 ole32 user32 gdi32 windowscodecs d3d11 dxgi d3dcompiler)
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
  foreach(target BFVRPresenter BFVRSharedTextureConsumerProbe)
    add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>/assets/SettingsMenu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_bg.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_text.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_BorderFrame.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_BorderFrameHover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_bg.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_menuhover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_deployhover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_1hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_2hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_3hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_4hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_5hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_6hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F9hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F10hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F11hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F12hover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_frame.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_text.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_Cursor.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripBG.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripDecoupleHover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripMapHover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripSettingsHover.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripText.png"
        "$<TARGET_FILE_DIR:${target}>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/SettingsMenu"
      "$<TARGET_FILE_DIR:${target}>/assets/SettingsMenu")
  endforeach()
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

set(BFVR_AUDIO_RUNTIME_DIRECTORY
  "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/audio/win32")
foreach(audio_runtime BFVRDSoal.dll BFVROpenALSoft.dll)
  if(NOT EXISTS "${BFVR_AUDIO_RUNTIME_DIRECTORY}/${audio_runtime}")
    message(FATAL_ERROR
      "The pinned BFVR HRTF runtime is missing ${audio_runtime}.")
  endif()
endforeach()

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
  audio/HrtfAudioBridge.cpp
  audio/HrtfBufferPolicy.cpp
  audio/HrtfListenerMath.cpp
  audio/HrtfMenuSoundPolicy.cpp
  client/BFVRClient.cpp
  client/D3D8CallInventory.cpp
  client/ControllerInputCache.cpp
  client/ControllerInputOverlay.cpp
  client/CrosshairOverlay.cpp
  client/D3D8WorldCrosshairRenderer.cpp
  client/MainMenuOverlay.cpp
  client/MenuPointerOverlay.cpp
  client/MountedWeaponAimResolver.cpp
  client/ScopedOffHandSupportPoseCache.cpp
  client/ScopeViewOverlay.cpp
  client/StartupMenuPresentation.cpp
  client/TrackedScopeAim.cpp
  client/D3D8ImportRoute.cpp
  client/DirectSoundImportRoute.cpp
  client/D3D8PresentationConfiguration.cpp
  client/D3D8RenderViewPoseHook.cpp
  client/BFSoldierVrMotionFilter.cpp
  client/BFSoldierFirstPersonArmProbe.cpp
  client/BFSoldierLeftGripRotationBinding.cpp
  client/BFSoldierNativeArmIk.cpp
  client/BFSoldierPrimarySupportPoseCache.cpp
  client/BFSoldierRightGripRotationBinding.cpp
  client/BFSoldierNativeArmPole.cpp
  client/BFSoldierOffHandSupportBinding.cpp
  client/BFSoldierOffHandWeaponSteering.cpp
  client/BFSoldierTrackedHandPose.cpp
  client/D3D8RuntimePosePolicy.cpp
  client/D3D8TrackingAnchor.cpp
  client/D3D8RuntimeDiagnostics.cpp
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
  client/internal/D3D8StereoPairWaterDiagnostics.inl
  client/internal/D3D8StereoPairWaterMask.inl
  openxr/OpenXRBootstrap.cpp
  presenter/SharedControlChannel.cpp
  presenter/D3DSystemRuntime.cpp
  presenter/SharedTextureProducer.cpp
  settings/UserSettings.cpp
)

add_library(BFVROpenALHrtfRouter SHARED
  audio/OpenALHrtfRouter.cpp
  audio/OpenALHrtfRouter.def
)

add_library(BFVRD3D8To9FlatClient SHARED
  client/BFVRD3D8To9FlatClient.cpp
  client/D3D8ImportRoute.cpp
  client/D3D8RuntimeRedirect.cpp
)

add_library(BFVRStereoMath STATIC
  stereo/ArmPoleVectorMath.cpp
  stereo/D3D8DrawPolicy.cpp
  stereo/D3D8FirstPersonArmPolicy.cpp
  stereo/D3D8FrameCompositionPolicy.cpp
  stereo/D3D8SemanticDrawPolicy.cpp
  stereo/D3D8ShaderTransform.cpp
  stereo/D3D8WaterReflectionTextureBasis.cpp
  stereo/D3D8WeaponDrawPolicy.cpp
  stereo/DirectionalLocomotion.cpp
  stereo/OffHandSupportPolicy.cpp
  stereo/OffHandWeaponSteeringMath.cpp
  stereo/MountedCameraMath.cpp
  stereo/QuickMenuInteraction.cpp
  stereo/QuickMenuMirrorMath.cpp
  stereo/ScopeViewMath.cpp
  stereo/SurfaceVehicleDriveMath.cpp
  stereo/WeaponMotionPolicy.cpp
  stereo/WeaponFireAimMath.cpp
  stereo/StereoMath.cpp
  stereo/TreeAngleSlicePolicy.cpp
  stereo/UiPointerMath.cpp
  stereo/VehicleMotionAimMath.cpp
  stereo/WorldCrosshairMath.cpp
  stereo/WeaponPoseMath.cpp
)

add_executable(BFVRLoader
  loader/BFVRLoader.cpp
)

add_executable(BFVROpenXRProbe
  client/QuickMenuArt.cpp
  client/SettingsMenuArt.cpp
  openxr/BFVROpenXRProbe.cpp
  openxr/OpenXRBootstrap.cpp
  openxr/OpenXRQuickMenu.cpp
  openxr/OpenXRPresentation.cpp
  openxr/OpenXRPresentationSupport.cpp
  openxr/OpenXRScopeOverlayLayer.cpp
  openxr/OpenXRTrackingBasis.cpp
  settings/UserSettings.cpp
  stereo/SettingsMenuInteraction.cpp
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

add_executable(BFVRAmbientOcclusionDepthProbe
  presenter/BFVRAmbientOcclusionDepthProbe.cpp
  presenter/D3DSystemRuntime.cpp
)

add_executable(BFVRStereoMathTests
  client/D3D8RuntimePosePolicy.cpp
  client/D3D8TrackingAnchor.cpp
  client/D3D8SpriteShaderTransform.cpp
  client/D3D8TreeSpriteShaderTransform.cpp
  client/D3D8StereoShaderTransform.cpp
  client/D3D8StereoProbeRecords.cpp
  stereo/StereoMathTests.cpp
)

add_executable(BFVRWaterReflectionTextureBasisTests
  stereo/D3D8WaterReflectionTextureBasisTests.cpp
)

add_executable(BFVRWeaponFireAimMathTests
  stereo/WeaponFireAimMathTests.cpp
)

add_executable(BFVROffHandSupportPolicyTests
  client/BFSoldierLeftGripRotationBinding.cpp
  client/BFSoldierOffHandSupportBinding.cpp
  client/BFSoldierOffHandWeaponSteering.cpp
  client/BFSoldierPrimarySupportPoseCache.cpp
  client/BFSoldierTrackedHandPose.cpp
  client/ScopedOffHandSupportPoseCache.cpp
  stereo/OffHandSupportPolicyTests.cpp
)

add_executable(BFVRArmPoleVectorMathTests
  stereo/ArmPoleVectorMathTests.cpp
)

add_executable(BFVRMainMenuOverlayTests
  client/MainMenuOverlay.cpp
  client/MainMenuOverlayTests.cpp
)

add_executable(BFVRQuickMenuTests
  client/QuickMenuArt.cpp
  stereo/QuickMenuTests.cpp
)

add_executable(BFVRSettingsMenuTests
  client/SettingsMenuArt.cpp
  stereo/SettingsMenuInteraction.cpp
  stereo/SettingsMenuTests.cpp
)

add_executable(BFVRUserSettingsTests
  settings/UserSettings.cpp
  settings/UserSettingsTests.cpp
)

add_executable(BFVRVehicleMotionAimMathTests
  stereo/VehicleMotionAimMathTests.cpp
)

add_executable(BFVRDirectionalLocomotionTests
  stereo/DirectionalLocomotionTests.cpp
)

add_executable(BFVRSurfaceVehicleDriveMathTests
  stereo/SurfaceVehicleDriveMathTests.cpp
)

add_executable(BFVRTrackingAnchorTests
  client/D3D8TrackingAnchor.cpp
  client/D3D8TrackingAnchorTests.cpp
)

add_executable(BFVRMountedCameraMathTests
  stereo/MountedCameraMathTests.cpp
)

add_executable(BFVRScopeViewMathTests
  stereo/ScopeViewMathTests.cpp
)

add_executable(BFVROpenXRScopeOverlayLayerTests
  openxr/OpenXRScopeOverlayLayer.cpp
  openxr/OpenXRScopeOverlayLayerTests.cpp
)

add_executable(BFVRWorldCrosshairMathTests
  stereo/WorldCrosshairMathTests.cpp
)

add_executable(BFVRRuntimeDiagnosticsTests
  client/D3D8RuntimeDiagnostics.cpp
  client/D3D8RuntimeDiagnosticsTests.cpp
)

add_executable(BFVRHrtfListenerMathTests
  audio/HrtfListenerMath.cpp
  audio/HrtfListenerMathTests.cpp
)

add_executable(BFVRHrtfBufferPolicyTests
  audio/HrtfBufferPolicy.cpp
  audio/HrtfBufferPolicyTests.cpp
)

add_executable(BFVRHrtfMenuSoundPolicyTests
  audio/HrtfMenuSoundPolicy.cpp
  audio/HrtfMenuSoundPolicyTests.cpp
)

add_executable(BFVRHrtfBackendProbe
  audio/HrtfBackendProbe.cpp
)

add_executable(BFVRDirectSoundImportRouteTests
  client/DirectSoundImportRoute.cpp
  client/DirectSoundImportRouteTests.cpp
)

add_executable(BFVRSharedControlChannelTests
  presenter/SharedControlChannel.cpp
  presenter/SharedControlChannelTests.cpp
)

add_executable(BFVRRfaInspect
  tools/RfaInspect.cpp
)

foreach(target BFVRClient BFVROpenALHrtfRouter BFVRD3D8To9FlatClient BFVRLoader BFVROpenXRProbe BFVRSharedTextureProducerProbe BFVRD3D8To9SharedSurfaceProbe BFVRAmbientOcclusionDepthProbe BFVRStereoMath BFVRStereoMathTests BFVRWaterReflectionTextureBasisTests BFVRWeaponFireAimMathTests BFVROffHandSupportPolicyTests BFVRArmPoleVectorMathTests BFVRMainMenuOverlayTests BFVRQuickMenuTests BFVRSettingsMenuTests BFVRUserSettingsTests BFVRVehicleMotionAimMathTests BFVRDirectionalLocomotionTests BFVRSurfaceVehicleDriveMathTests BFVRTrackingAnchorTests BFVRMountedCameraMathTests BFVRScopeViewMathTests BFVROpenXRScopeOverlayLayerTests BFVRWorldCrosshairMathTests BFVRRuntimeDiagnosticsTests BFVRHrtfListenerMathTests BFVRHrtfBufferPolicyTests BFVRHrtfMenuSoundPolicyTests BFVRHrtfBackendProbe BFVRDirectSoundImportRouteTests BFVRSharedControlChannelTests BFVRRfaInspect)
  target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  endif()
endforeach()

target_link_libraries(BFVRClient PRIVATE
  kernel32 advapi32 ole32 user32 windowscodecs)
target_link_libraries(BFVRD3D8To9FlatClient PRIVATE kernel32)
target_link_libraries(BFVRLoader PRIVATE kernel32)
target_link_libraries(BFVROpenXRProbe PRIVATE
  kernel32 ole32 gdi32 windowscodecs d3d11 dxgi BFVRStereoMath)
target_link_libraries(BFVRSharedTextureProducerProbe PRIVATE kernel32)
target_link_libraries(BFVRD3D8To9SharedSurfaceProbe PRIVATE kernel32 user32)
target_link_libraries(BFVRAmbientOcclusionDepthProbe PRIVATE kernel32 user32)
target_include_directories(BFVRSharedTextureProducerProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRD3D8To9SharedSurfaceProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${BFVR_D3D8TO9_ROOT}/source")
target_include_directories(BFVRAmbientOcclusionDepthProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${BFVR_D3D8TO9_ROOT}/source")
target_include_directories(BFVROpenALHrtfRouter PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRHrtfBackendProbe PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRHrtfBufferPolicyTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRHrtfMenuSoundPolicyTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRDirectSoundImportRouteTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(BFVRClient PRIVATE minhook BFVRStereoMath)
target_include_directories(BFVRD3D8To9FlatClient PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRStereoMath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(BFVRStereoMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRWaterReflectionTextureBasisTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRWeaponFireAimMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVROffHandSupportPolicyTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRArmPoleVectorMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRMainMenuOverlayTests PRIVATE
  ole32 user32 windowscodecs)
target_link_libraries(BFVRQuickMenuTests PRIVATE
  BFVRStereoMath ole32 user32 windowscodecs)
target_link_libraries(BFVRSettingsMenuTests PRIVATE
  BFVRStereoMath ole32 user32 gdi32 windowscodecs)
target_link_libraries(BFVRVehicleMotionAimMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRDirectionalLocomotionTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRSurfaceVehicleDriveMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRTrackingAnchorTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRMountedCameraMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRScopeViewMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVROpenXRScopeOverlayLayerTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRWorldCrosshairMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRHrtfListenerMathTests PRIVATE BFVRStereoMath)
target_link_libraries(BFVRDirectSoundImportRouteTests PRIVATE dsound)
target_include_directories(BFVRMainMenuOverlayTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRQuickMenuTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRSettingsMenuTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRUserSettingsTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRRuntimeDiagnosticsTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVRSharedControlChannelTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BFVROpenXRScopeOverlayLayerTests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${BFVR_OPENXR_INCLUDE_DIRECTORY}")

include(CTest)
if(BUILD_TESTING)
  add_test(NAME BFVRStereoMathTests COMMAND BFVRStereoMathTests)
  add_test(
    NAME BFVRWaterReflectionTextureBasisTests
    COMMAND BFVRWaterReflectionTextureBasisTests)
  add_test(
    NAME BFVRWeaponFireAimMathTests
    COMMAND BFVRWeaponFireAimMathTests)
  add_test(
    NAME BFVROffHandSupportPolicyTests
    COMMAND BFVROffHandSupportPolicyTests)
  add_test(
    NAME BFVRArmPoleVectorMathTests
    COMMAND BFVRArmPoleVectorMathTests)
  add_test(
    NAME BFVRMainMenuOverlayTests
    COMMAND BFVRMainMenuOverlayTests
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
  add_test(
    NAME BFVRQuickMenuTests
    COMMAND BFVRQuickMenuTests
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
  add_test(
    NAME BFVRSettingsMenuTests
    COMMAND BFVRSettingsMenuTests
      "${CMAKE_CURRENT_SOURCE_DIR}/../assets/SettingsMenu")
  add_test(
    NAME BFVRUserSettingsTests
    COMMAND BFVRUserSettingsTests)
  add_test(
    NAME BFVRVehicleMotionAimMathTests
    COMMAND BFVRVehicleMotionAimMathTests)
  add_test(
    NAME BFVRDirectionalLocomotionTests
    COMMAND BFVRDirectionalLocomotionTests)
  add_test(
    NAME BFVRSurfaceVehicleDriveMathTests
    COMMAND BFVRSurfaceVehicleDriveMathTests)
  add_test(
    NAME BFVRTrackingAnchorTests
    COMMAND BFVRTrackingAnchorTests)
  add_test(
    NAME BFVRMountedCameraMathTests
    COMMAND BFVRMountedCameraMathTests)
  add_test(
    NAME BFVRScopeViewMathTests
    COMMAND BFVRScopeViewMathTests)
  add_test(
    NAME BFVROpenXRScopeOverlayLayerTests
    COMMAND BFVROpenXRScopeOverlayLayerTests)
  add_test(
    NAME BFVRWorldCrosshairMathTests
    COMMAND BFVRWorldCrosshairMathTests)
  add_test(
    NAME BFVRRuntimeDiagnosticsTests
    COMMAND BFVRRuntimeDiagnosticsTests)
  add_test(
    NAME BFVRHrtfListenerMathTests
    COMMAND BFVRHrtfListenerMathTests)
  add_test(
    NAME BFVRHrtfBufferPolicyTests
    COMMAND BFVRHrtfBufferPolicyTests)
  add_test(
    NAME BFVRHrtfMenuSoundPolicyTests
    COMMAND BFVRHrtfMenuSoundPolicyTests)
  add_test(
    NAME BFVRDirectSoundImportRouteTests
    COMMAND BFVRDirectSoundImportRouteTests)
  add_test(
    NAME BFVRSharedControlChannelTests
    COMMAND BFVRSharedControlChannelTests)
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

add_custom_command(TARGET BFVRClient POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:BFVRClient>/assets"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:BFVRClient>/assets/SettingsMenu"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_bg.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_text.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_BorderFrame.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/BackToGame_BorderFrameHover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_bg.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_menuhover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_deployhover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_1hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_2hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_3hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_4hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_5hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_6hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F9hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F10hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F11hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_F12hover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_frame.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_text.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_Cursor.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripBG.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripDecoupleHover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripMapHover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripSettingsHover.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/QM_stripText.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/Crosshair.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/HitMarker.png"
    "$<TARGET_FILE_DIR:BFVRClient>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/SettingsMenu"
    "$<TARGET_FILE_DIR:BFVRClient>/assets/SettingsMenu")

add_dependencies(BFVRClient BFVROpenALHrtfRouter)
add_custom_command(TARGET BFVRClient POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:BFVRClient>/runtime/audio/win32"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${BFVR_AUDIO_RUNTIME_DIRECTORY}/BFVRDSoal.dll"
    "${BFVR_AUDIO_RUNTIME_DIRECTORY}/BFVROpenALSoft.dll"
    "${BFVR_AUDIO_RUNTIME_DIRECTORY}/alsoft-hrtf-reference.ini"
    "$<TARGET_FILE_DIR:BFVRClient>/runtime/audio/win32"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:BFVROpenALHrtfRouter>"
    "$<TARGET_FILE_DIR:BFVRClient>/runtime/audio/win32/dsoal-aldrv.dll")

add_dependencies(BFVRHrtfBackendProbe BFVROpenALHrtfRouter)
add_custom_command(TARGET BFVRHrtfBackendProbe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "$<TARGET_FILE_DIR:BFVRHrtfBackendProbe>/runtime/audio/win32"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${BFVR_AUDIO_RUNTIME_DIRECTORY}/BFVRDSoal.dll"
    "${BFVR_AUDIO_RUNTIME_DIRECTORY}/BFVROpenALSoft.dll"
    "$<TARGET_FILE_DIR:BFVRHrtfBackendProbe>/runtime/audio/win32"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:BFVROpenALHrtfRouter>"
    "$<TARGET_FILE_DIR:BFVRHrtfBackendProbe>/runtime/audio/win32/dsoal-aldrv.dll")

target_include_directories(BFVRClient PRIVATE
  "${BFVR_D3D8TO9_ROOT}/source")

set_target_properties(BFVRClient PROPERTIES OUTPUT_NAME "BFVRClient")
set_target_properties(BFVROpenALHrtfRouter PROPERTIES
  OUTPUT_NAME "dsoal-aldrv")
set_target_properties(BFVRD3D8To9FlatClient PROPERTIES
  OUTPUT_NAME "BFVRD3D8To9FlatClient")
set_target_properties(BFVRLoader PROPERTIES OUTPUT_NAME "BFVRLoader")
set_target_properties(BFVROpenXRProbe PROPERTIES OUTPUT_NAME "BFVROpenXRProbe")
set_target_properties(BFVRSharedTextureProducerProbe PROPERTIES
  OUTPUT_NAME "BFVRSharedTextureProducerProbe")
set_target_properties(BFVRD3D8To9SharedSurfaceProbe PROPERTIES
  OUTPUT_NAME "BFVRD3D8To9SharedSurfaceProbe")
set_target_properties(BFVRAmbientOcclusionDepthProbe PROPERTIES
  OUTPUT_NAME "BFVRAmbientOcclusionDepthProbe")

add_custom_target(BFVRD3D8To9RuntimeCopy ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:d3d8to9>"
    "$<TARGET_FILE_DIR:BFVRClient>/BFVRD3D8To9.dll"
  DEPENDS d3d8to9)
foreach(target BFVRClient BFVRD3D8To9FlatClient BFVRD3D8To9SharedSurfaceProbe BFVRAmbientOcclusionDepthProbe)
  add_dependencies(${target} BFVRD3D8To9RuntimeCopy)
endforeach()

# These native targets are intentionally just an observation prototype.  The
# release launcher/client layout is not produced until the passive trace has
# been validated on a reproducible local gameplay baseline.
