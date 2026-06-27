/* SPDX-License-Identifier: MIT */

/* rhi_lib_vulkan.cpp - libretro Vulkan GPU backend for Beetle PSX HW,
 * with the entire parallel-psx Vulkan + FBAtlas + HD texture-tracker
 * stack folded in. Previously the renderer lived under
 * parallel-psx/renderer/renderer.{hpp,cpp}; that directory is gone
 * and its contents (header content first, then implementation) sit
 * directly below the libretro plumbing's preamble. */

#include "rhi_lib_vulkan.h"

#include "rhi_intf.h" /* FPS and audio sample rate macros */
#include "rhi_defer.h"
#include "tt_trace.h"

/* Force-inline shim for converted accessors. Must resolve to a real
 * always-inline so that primitives converted from C++ inline methods to C89
 * free functions retain identical codegen (verified for the FNV-1a hasher). */
#if defined(_MSC_VER)
#  define RHI_INLINE static __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define RHI_INLINE static __inline__ __attribute__((always_inline))
#else
#  define RHI_INLINE static
#endif

/* === folded parallel-psx/volk/volk.h + volk.c === */
/* Folded from parallel-psx/volk/volk.h ((c) 2018 Arseny Kapoulkine, MIT). */

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VULKAN_H_
#include <vulkan/vulkan.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* VOLK_GENERATE_PROTOTYPES_H */
#if defined(VK_VERSION_1_0)
extern PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
extern PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
extern PFN_vkAllocateMemory vkAllocateMemory;
extern PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
extern PFN_vkBindBufferMemory vkBindBufferMemory;
extern PFN_vkBindImageMemory vkBindImageMemory;
extern PFN_vkCmdBeginQuery vkCmdBeginQuery;
extern PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
extern PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
extern PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
extern PFN_vkCmdBindPipeline vkCmdBindPipeline;
extern PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
extern PFN_vkCmdBlitImage vkCmdBlitImage;
extern PFN_vkCmdClearAttachments vkCmdClearAttachments;
extern PFN_vkCmdClearColorImage vkCmdClearColorImage;
extern PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
extern PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
extern PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
extern PFN_vkCmdCopyImage vkCmdCopyImage;
extern PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
extern PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
extern PFN_vkCmdDispatch vkCmdDispatch;
extern PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
extern PFN_vkCmdDraw vkCmdDraw;
extern PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
extern PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
extern PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
extern PFN_vkCmdEndQuery vkCmdEndQuery;
extern PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
extern PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
extern PFN_vkCmdFillBuffer vkCmdFillBuffer;
extern PFN_vkCmdNextSubpass vkCmdNextSubpass;
extern PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
extern PFN_vkCmdPushConstants vkCmdPushConstants;
extern PFN_vkCmdResetEvent vkCmdResetEvent;
extern PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
extern PFN_vkCmdResolveImage vkCmdResolveImage;
extern PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
extern PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
extern PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
extern PFN_vkCmdSetEvent vkCmdSetEvent;
extern PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
extern PFN_vkCmdSetScissor vkCmdSetScissor;
extern PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
extern PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
extern PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
extern PFN_vkCmdSetViewport vkCmdSetViewport;
extern PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
extern PFN_vkCmdWaitEvents vkCmdWaitEvents;
extern PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
extern PFN_vkCreateBuffer vkCreateBuffer;
extern PFN_vkCreateBufferView vkCreateBufferView;
extern PFN_vkCreateCommandPool vkCreateCommandPool;
extern PFN_vkCreateComputePipelines vkCreateComputePipelines;
extern PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
extern PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
extern PFN_vkCreateDevice vkCreateDevice;
extern PFN_vkCreateEvent vkCreateEvent;
extern PFN_vkCreateFence vkCreateFence;
extern PFN_vkCreateFramebuffer vkCreateFramebuffer;
extern PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
extern PFN_vkCreateImage vkCreateImage;
extern PFN_vkCreateImageView vkCreateImageView;
extern PFN_vkCreateInstance vkCreateInstance;
extern PFN_vkCreatePipelineCache vkCreatePipelineCache;
extern PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
extern PFN_vkCreateQueryPool vkCreateQueryPool;
extern PFN_vkCreateRenderPass vkCreateRenderPass;
extern PFN_vkCreateSampler vkCreateSampler;
extern PFN_vkCreateSemaphore vkCreateSemaphore;
extern PFN_vkCreateShaderModule vkCreateShaderModule;
extern PFN_vkDestroyBuffer vkDestroyBuffer;
extern PFN_vkDestroyBufferView vkDestroyBufferView;
extern PFN_vkDestroyCommandPool vkDestroyCommandPool;
extern PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
extern PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
extern PFN_vkDestroyDevice vkDestroyDevice;
extern PFN_vkDestroyEvent vkDestroyEvent;
extern PFN_vkDestroyFence vkDestroyFence;
extern PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
extern PFN_vkDestroyImage vkDestroyImage;
extern PFN_vkDestroyImageView vkDestroyImageView;
extern PFN_vkDestroyInstance vkDestroyInstance;
extern PFN_vkDestroyPipeline vkDestroyPipeline;
extern PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
extern PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
extern PFN_vkDestroyQueryPool vkDestroyQueryPool;
extern PFN_vkDestroyRenderPass vkDestroyRenderPass;
extern PFN_vkDestroySampler vkDestroySampler;
extern PFN_vkDestroySemaphore vkDestroySemaphore;
extern PFN_vkDestroyShaderModule vkDestroyShaderModule;
extern PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
extern PFN_vkEndCommandBuffer vkEndCommandBuffer;
extern PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
extern PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
extern PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
extern PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
extern PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
extern PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
extern PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
extern PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
extern PFN_vkFreeMemory vkFreeMemory;
extern PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
extern PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
extern PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
extern PFN_vkGetDeviceQueue vkGetDeviceQueue;
extern PFN_vkGetEventStatus vkGetEventStatus;
extern PFN_vkGetFenceStatus vkGetFenceStatus;
extern PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
extern PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
extern PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
extern PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
extern PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
extern PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
extern PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
extern PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
extern PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
extern PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
extern PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
extern PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
extern PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
extern PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
extern PFN_vkMapMemory vkMapMemory;
extern PFN_vkMergePipelineCaches vkMergePipelineCaches;
extern PFN_vkQueueBindSparse vkQueueBindSparse;
extern PFN_vkQueueSubmit vkQueueSubmit;
extern PFN_vkQueueWaitIdle vkQueueWaitIdle;
extern PFN_vkResetCommandBuffer vkResetCommandBuffer;
extern PFN_vkResetCommandPool vkResetCommandPool;
extern PFN_vkResetDescriptorPool vkResetDescriptorPool;
extern PFN_vkResetEvent vkResetEvent;
extern PFN_vkResetFences vkResetFences;
extern PFN_vkSetEvent vkSetEvent;
extern PFN_vkUnmapMemory vkUnmapMemory;
extern PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
extern PFN_vkWaitForFences vkWaitForFences;
#endif /* defined(VK_VERSION_1_0) */
#if defined(VK_VERSION_1_1)
extern PFN_vkBindBufferMemory2 vkBindBufferMemory2;
extern PFN_vkBindImageMemory2 vkBindImageMemory2;
extern PFN_vkCmdDispatchBase vkCmdDispatchBase;
extern PFN_vkCmdSetDeviceMask vkCmdSetDeviceMask;
extern PFN_vkCreateDescriptorUpdateTemplate vkCreateDescriptorUpdateTemplate;
extern PFN_vkCreateSamplerYcbcrConversion vkCreateSamplerYcbcrConversion;
extern PFN_vkDestroyDescriptorUpdateTemplate vkDestroyDescriptorUpdateTemplate;
extern PFN_vkDestroySamplerYcbcrConversion vkDestroySamplerYcbcrConversion;
extern PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
extern PFN_vkEnumeratePhysicalDeviceGroups vkEnumeratePhysicalDeviceGroups;
extern PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
extern PFN_vkGetDescriptorSetLayoutSupport vkGetDescriptorSetLayoutSupport;
extern PFN_vkGetDeviceGroupPeerMemoryFeatures vkGetDeviceGroupPeerMemoryFeatures;
extern PFN_vkGetDeviceQueue2 vkGetDeviceQueue2;
extern PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
extern PFN_vkGetImageSparseMemoryRequirements2 vkGetImageSparseMemoryRequirements2;
extern PFN_vkGetPhysicalDeviceExternalBufferProperties vkGetPhysicalDeviceExternalBufferProperties;
extern PFN_vkGetPhysicalDeviceExternalFenceProperties vkGetPhysicalDeviceExternalFenceProperties;
extern PFN_vkGetPhysicalDeviceExternalSemaphoreProperties vkGetPhysicalDeviceExternalSemaphoreProperties;
extern PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
extern PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;
extern PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;
extern PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2;
extern PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
extern PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2;
extern PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 vkGetPhysicalDeviceSparseImageFormatProperties2;
extern PFN_vkTrimCommandPool vkTrimCommandPool;
extern PFN_vkUpdateDescriptorSetWithTemplate vkUpdateDescriptorSetWithTemplate;
#endif /* defined(VK_VERSION_1_1) */
#if defined(VK_AMD_buffer_marker)
extern PFN_vkCmdWriteBufferMarkerAMD vkCmdWriteBufferMarkerAMD;
#endif /* defined(VK_AMD_buffer_marker) */
#if defined(VK_AMD_draw_indirect_count)
extern PFN_vkCmdDrawIndexedIndirectCountAMD vkCmdDrawIndexedIndirectCountAMD;
extern PFN_vkCmdDrawIndirectCountAMD vkCmdDrawIndirectCountAMD;
#endif /* defined(VK_AMD_draw_indirect_count) */
#if defined(VK_AMD_shader_info)
extern PFN_vkGetShaderInfoAMD vkGetShaderInfoAMD;
#endif /* defined(VK_AMD_shader_info) */
#if defined(VK_ANDROID_external_memory_android_hardware_buffer)
extern PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;
extern PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;
#endif /* defined(VK_ANDROID_external_memory_android_hardware_buffer) */
#if defined(VK_EXT_acquire_xlib_display)
extern PFN_vkAcquireXlibDisplayEXT vkAcquireXlibDisplayEXT;
extern PFN_vkGetRandROutputDisplayEXT vkGetRandROutputDisplayEXT;
#endif /* defined(VK_EXT_acquire_xlib_display) */
#if defined(VK_EXT_calibrated_timestamps)
extern PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;
extern PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vkGetPhysicalDeviceCalibrateableTimeDomainsEXT;
#endif /* defined(VK_EXT_calibrated_timestamps) */
#if defined(VK_EXT_conditional_rendering)
extern PFN_vkCmdBeginConditionalRenderingEXT vkCmdBeginConditionalRenderingEXT;
extern PFN_vkCmdEndConditionalRenderingEXT vkCmdEndConditionalRenderingEXT;
#endif /* defined(VK_EXT_conditional_rendering) */
#if defined(VK_EXT_debug_marker)
extern PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBeginEXT;
extern PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEndEXT;
extern PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsertEXT;
extern PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectNameEXT;
extern PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTagEXT;
#endif /* defined(VK_EXT_debug_marker) */
#if defined(VK_EXT_debug_report)
extern PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
extern PFN_vkDebugReportMessageEXT vkDebugReportMessageEXT;
extern PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;
#endif /* defined(VK_EXT_debug_report) */
#if defined(VK_EXT_debug_utils)
extern PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
extern PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;
extern PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT;
extern PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
extern PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
extern PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT;
extern PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT;
extern PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT;
extern PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
extern PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT;
extern PFN_vkSubmitDebugUtilsMessageEXT vkSubmitDebugUtilsMessageEXT;
#endif /* defined(VK_EXT_debug_utils) */
#if defined(VK_EXT_direct_mode_display)
extern PFN_vkReleaseDisplayEXT vkReleaseDisplayEXT;
#endif /* defined(VK_EXT_direct_mode_display) */
#if defined(VK_EXT_discard_rectangles)
extern PFN_vkCmdSetDiscardRectangleEXT vkCmdSetDiscardRectangleEXT;
#endif /* defined(VK_EXT_discard_rectangles) */
#if defined(VK_EXT_display_control)
extern PFN_vkDisplayPowerControlEXT vkDisplayPowerControlEXT;
extern PFN_vkGetSwapchainCounterEXT vkGetSwapchainCounterEXT;
extern PFN_vkRegisterDeviceEventEXT vkRegisterDeviceEventEXT;
extern PFN_vkRegisterDisplayEventEXT vkRegisterDisplayEventEXT;
#endif /* defined(VK_EXT_display_control) */
#if defined(VK_EXT_display_surface_counter)
extern PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT vkGetPhysicalDeviceSurfaceCapabilities2EXT;
#endif /* defined(VK_EXT_display_surface_counter) */
#if defined(VK_EXT_external_memory_host)
extern PFN_vkGetMemoryHostPointerPropertiesEXT vkGetMemoryHostPointerPropertiesEXT;
#endif /* defined(VK_EXT_external_memory_host) */
#if defined(VK_EXT_hdr_metadata)
extern PFN_vkSetHdrMetadataEXT vkSetHdrMetadataEXT;
#endif /* defined(VK_EXT_hdr_metadata) */
#if defined(VK_EXT_image_drm_format_modifier)
extern PFN_vkGetImageDrmFormatModifierPropertiesEXT vkGetImageDrmFormatModifierPropertiesEXT;
#endif /* defined(VK_EXT_image_drm_format_modifier) */
#if defined(VK_EXT_sample_locations)
extern PFN_vkCmdSetSampleLocationsEXT vkCmdSetSampleLocationsEXT;
extern PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT vkGetPhysicalDeviceMultisamplePropertiesEXT;
#endif /* defined(VK_EXT_sample_locations) */
#if defined(VK_EXT_transform_feedback)
extern PFN_vkCmdBeginQueryIndexedEXT vkCmdBeginQueryIndexedEXT;
extern PFN_vkCmdBeginTransformFeedbackEXT vkCmdBeginTransformFeedbackEXT;
extern PFN_vkCmdBindTransformFeedbackBuffersEXT vkCmdBindTransformFeedbackBuffersEXT;
extern PFN_vkCmdDrawIndirectByteCountEXT vkCmdDrawIndirectByteCountEXT;
extern PFN_vkCmdEndQueryIndexedEXT vkCmdEndQueryIndexedEXT;
extern PFN_vkCmdEndTransformFeedbackEXT vkCmdEndTransformFeedbackEXT;
#endif /* defined(VK_EXT_transform_feedback) */
#if defined(VK_EXT_validation_cache)
extern PFN_vkCreateValidationCacheEXT vkCreateValidationCacheEXT;
extern PFN_vkDestroyValidationCacheEXT vkDestroyValidationCacheEXT;
extern PFN_vkGetValidationCacheDataEXT vkGetValidationCacheDataEXT;
extern PFN_vkMergeValidationCachesEXT vkMergeValidationCachesEXT;
#endif /* defined(VK_EXT_validation_cache) */
#if defined(VK_FUCHSIA_imagepipe_surface)
extern PFN_vkCreateImagePipeSurfaceFUCHSIA vkCreateImagePipeSurfaceFUCHSIA;
#endif /* defined(VK_FUCHSIA_imagepipe_surface) */
#if defined(VK_GOOGLE_display_timing)
extern PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE;
extern PFN_vkGetRefreshCycleDurationGOOGLE vkGetRefreshCycleDurationGOOGLE;
#endif /* defined(VK_GOOGLE_display_timing) */
#if defined(VK_KHR_android_surface)
extern PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif /* defined(VK_KHR_android_surface) */
#if defined(VK_KHR_bind_memory2)
extern PFN_vkBindBufferMemory2KHR vkBindBufferMemory2KHR;
extern PFN_vkBindImageMemory2KHR vkBindImageMemory2KHR;
#endif /* defined(VK_KHR_bind_memory2) */
#if defined(VK_KHR_create_renderpass2)
extern PFN_vkCmdBeginRenderPass2KHR vkCmdBeginRenderPass2KHR;
extern PFN_vkCmdEndRenderPass2KHR vkCmdEndRenderPass2KHR;
extern PFN_vkCmdNextSubpass2KHR vkCmdNextSubpass2KHR;
extern PFN_vkCreateRenderPass2KHR vkCreateRenderPass2KHR;
#endif /* defined(VK_KHR_create_renderpass2) */
#if defined(VK_KHR_descriptor_update_template)
extern PFN_vkCreateDescriptorUpdateTemplateKHR vkCreateDescriptorUpdateTemplateKHR;
extern PFN_vkDestroyDescriptorUpdateTemplateKHR vkDestroyDescriptorUpdateTemplateKHR;
extern PFN_vkUpdateDescriptorSetWithTemplateKHR vkUpdateDescriptorSetWithTemplateKHR;
#endif /* defined(VK_KHR_descriptor_update_template) */
#if defined(VK_KHR_device_group)
extern PFN_vkCmdDispatchBaseKHR vkCmdDispatchBaseKHR;
extern PFN_vkCmdSetDeviceMaskKHR vkCmdSetDeviceMaskKHR;
extern PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR vkGetDeviceGroupPeerMemoryFeaturesKHR;
#endif /* defined(VK_KHR_device_group) */
#if defined(VK_KHR_device_group_creation)
extern PFN_vkEnumeratePhysicalDeviceGroupsKHR vkEnumeratePhysicalDeviceGroupsKHR;
#endif /* defined(VK_KHR_device_group_creation) */
#if defined(VK_KHR_display)
extern PFN_vkCreateDisplayModeKHR vkCreateDisplayModeKHR;
extern PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
extern PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
extern PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
extern PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR;
extern PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
extern PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
#endif /* defined(VK_KHR_display) */
#if defined(VK_KHR_display_swapchain)
extern PFN_vkCreateSharedSwapchainsKHR vkCreateSharedSwapchainsKHR;
#endif /* defined(VK_KHR_display_swapchain) */
#if defined(VK_KHR_draw_indirect_count)
extern PFN_vkCmdDrawIndexedIndirectCountKHR vkCmdDrawIndexedIndirectCountKHR;
extern PFN_vkCmdDrawIndirectCountKHR vkCmdDrawIndirectCountKHR;
#endif /* defined(VK_KHR_draw_indirect_count) */
#if defined(VK_KHR_external_fence_capabilities)
extern PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR vkGetPhysicalDeviceExternalFencePropertiesKHR;
#endif /* defined(VK_KHR_external_fence_capabilities) */
#if defined(VK_KHR_external_fence_fd)
extern PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
extern PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
#endif /* defined(VK_KHR_external_fence_fd) */
#if defined(VK_KHR_external_fence_win32)
extern PFN_vkGetFenceWin32HandleKHR vkGetFenceWin32HandleKHR;
extern PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR;
#endif /* defined(VK_KHR_external_fence_win32) */
#if defined(VK_KHR_external_memory_capabilities)
extern PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR vkGetPhysicalDeviceExternalBufferPropertiesKHR;
#endif /* defined(VK_KHR_external_memory_capabilities) */
#if defined(VK_KHR_external_memory_fd)
extern PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
extern PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR;
#endif /* defined(VK_KHR_external_memory_fd) */
#if defined(VK_KHR_external_memory_win32)
extern PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR;
extern PFN_vkGetMemoryWin32HandlePropertiesKHR vkGetMemoryWin32HandlePropertiesKHR;
#endif /* defined(VK_KHR_external_memory_win32) */
#if defined(VK_KHR_external_semaphore_capabilities)
extern PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR vkGetPhysicalDeviceExternalSemaphorePropertiesKHR;
#endif /* defined(VK_KHR_external_semaphore_capabilities) */
#if defined(VK_KHR_external_semaphore_fd)
extern PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
extern PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
#endif /* defined(VK_KHR_external_semaphore_fd) */
#if defined(VK_KHR_external_semaphore_win32)
extern PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR;
extern PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;
#endif /* defined(VK_KHR_external_semaphore_win32) */
#if defined(VK_KHR_get_display_properties2)
extern PFN_vkGetDisplayModeProperties2KHR vkGetDisplayModeProperties2KHR;
extern PFN_vkGetDisplayPlaneCapabilities2KHR vkGetDisplayPlaneCapabilities2KHR;
extern PFN_vkGetPhysicalDeviceDisplayPlaneProperties2KHR vkGetPhysicalDeviceDisplayPlaneProperties2KHR;
extern PFN_vkGetPhysicalDeviceDisplayProperties2KHR vkGetPhysicalDeviceDisplayProperties2KHR;
#endif /* defined(VK_KHR_get_display_properties2) */
#if defined(VK_KHR_get_memory_requirements2)
extern PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR;
extern PFN_vkGetImageMemoryRequirements2KHR vkGetImageMemoryRequirements2KHR;
extern PFN_vkGetImageSparseMemoryRequirements2KHR vkGetImageSparseMemoryRequirements2KHR;
#endif /* defined(VK_KHR_get_memory_requirements2) */
#if defined(VK_KHR_get_physical_device_properties2)
extern PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
extern PFN_vkGetPhysicalDeviceFormatProperties2KHR vkGetPhysicalDeviceFormatProperties2KHR;
extern PFN_vkGetPhysicalDeviceImageFormatProperties2KHR vkGetPhysicalDeviceImageFormatProperties2KHR;
extern PFN_vkGetPhysicalDeviceMemoryProperties2KHR vkGetPhysicalDeviceMemoryProperties2KHR;
extern PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
extern PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR vkGetPhysicalDeviceQueueFamilyProperties2KHR;
extern PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR vkGetPhysicalDeviceSparseImageFormatProperties2KHR;
#endif /* defined(VK_KHR_get_physical_device_properties2) */
#if defined(VK_KHR_get_surface_capabilities2)
extern PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR vkGetPhysicalDeviceSurfaceCapabilities2KHR;
extern PFN_vkGetPhysicalDeviceSurfaceFormats2KHR vkGetPhysicalDeviceSurfaceFormats2KHR;
#endif /* defined(VK_KHR_get_surface_capabilities2) */
#if defined(VK_KHR_maintenance1)
extern PFN_vkTrimCommandPoolKHR vkTrimCommandPoolKHR;
#endif /* defined(VK_KHR_maintenance1) */
#if defined(VK_KHR_maintenance3)
extern PFN_vkGetDescriptorSetLayoutSupportKHR vkGetDescriptorSetLayoutSupportKHR;
#endif /* defined(VK_KHR_maintenance3) */
#if defined(VK_KHR_push_descriptor)
extern PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;
#endif /* defined(VK_KHR_push_descriptor) */
#if defined(VK_KHR_sampler_ycbcr_conversion)
extern PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR;
extern PFN_vkDestroySamplerYcbcrConversionKHR vkDestroySamplerYcbcrConversionKHR;
#endif /* defined(VK_KHR_sampler_ycbcr_conversion) */
#if defined(VK_KHR_shared_presentable_image)
extern PFN_vkGetSwapchainStatusKHR vkGetSwapchainStatusKHR;
#endif /* defined(VK_KHR_shared_presentable_image) */
#if defined(VK_KHR_surface)
extern PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
extern PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
extern PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
extern PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
#endif /* defined(VK_KHR_surface) */
#if defined(VK_KHR_swapchain)
extern PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
extern PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
extern PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
extern PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
extern PFN_vkQueuePresentKHR vkQueuePresentKHR;
#endif /* defined(VK_KHR_swapchain) */
#if defined(VK_KHR_wayland_surface)
extern PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
extern PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR vkGetPhysicalDeviceWaylandPresentationSupportKHR;
#endif /* defined(VK_KHR_wayland_surface) */
#if defined(VK_KHR_win32_surface)
extern PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
extern PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR vkGetPhysicalDeviceWin32PresentationSupportKHR;
#endif /* defined(VK_KHR_win32_surface) */
#if defined(VK_KHR_xcb_surface)
extern PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
extern PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR vkGetPhysicalDeviceXcbPresentationSupportKHR;
#endif /* defined(VK_KHR_xcb_surface) */
#if defined(VK_KHR_xlib_surface)
extern PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
extern PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR vkGetPhysicalDeviceXlibPresentationSupportKHR;
#endif /* defined(VK_KHR_xlib_surface) */
#if defined(VK_MVK_ios_surface)
extern PFN_vkCreateIOSSurfaceMVK vkCreateIOSSurfaceMVK;
#endif /* defined(VK_MVK_ios_surface) */
#if defined(VK_MVK_macos_surface)
extern PFN_vkCreateMacOSSurfaceMVK vkCreateMacOSSurfaceMVK;
#endif /* defined(VK_MVK_macos_surface) */
#if defined(VK_NN_vi_surface)
extern PFN_vkCreateViSurfaceNN vkCreateViSurfaceNN;
#endif /* defined(VK_NN_vi_surface) */
#if defined(VK_NVX_device_generated_commands)
extern PFN_vkCmdProcessCommandsNVX vkCmdProcessCommandsNVX;
extern PFN_vkCmdReserveSpaceForCommandsNVX vkCmdReserveSpaceForCommandsNVX;
extern PFN_vkCreateIndirectCommandsLayoutNVX vkCreateIndirectCommandsLayoutNVX;
extern PFN_vkCreateObjectTableNVX vkCreateObjectTableNVX;
extern PFN_vkDestroyIndirectCommandsLayoutNVX vkDestroyIndirectCommandsLayoutNVX;
extern PFN_vkDestroyObjectTableNVX vkDestroyObjectTableNVX;
extern PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX;
extern PFN_vkRegisterObjectsNVX vkRegisterObjectsNVX;
extern PFN_vkUnregisterObjectsNVX vkUnregisterObjectsNVX;
#endif /* defined(VK_NVX_device_generated_commands) */
#if defined(VK_NVX_raytracing)
extern PFN_vkBindAccelerationStructureMemoryNVX vkBindAccelerationStructureMemoryNVX;
extern PFN_vkCmdBuildAccelerationStructureNVX vkCmdBuildAccelerationStructureNVX;
extern PFN_vkCmdCopyAccelerationStructureNVX vkCmdCopyAccelerationStructureNVX;
extern PFN_vkCmdTraceRaysNVX vkCmdTraceRaysNVX;
extern PFN_vkCmdWriteAccelerationStructurePropertiesNVX vkCmdWriteAccelerationStructurePropertiesNVX;
extern PFN_vkCompileDeferredNVX vkCompileDeferredNVX;
extern PFN_vkCreateAccelerationStructureNVX vkCreateAccelerationStructureNVX;
extern PFN_vkCreateRaytracingPipelinesNVX vkCreateRaytracingPipelinesNVX;
extern PFN_vkDestroyAccelerationStructureNVX vkDestroyAccelerationStructureNVX;
extern PFN_vkGetAccelerationStructureHandleNVX vkGetAccelerationStructureHandleNVX;
extern PFN_vkGetAccelerationStructureMemoryRequirementsNVX vkGetAccelerationStructureMemoryRequirementsNVX;
extern PFN_vkGetAccelerationStructureScratchMemoryRequirementsNVX vkGetAccelerationStructureScratchMemoryRequirementsNVX;
extern PFN_vkGetRaytracingShaderHandlesNVX vkGetRaytracingShaderHandlesNVX;
#endif /* defined(VK_NVX_raytracing) */
#if defined(VK_NV_clip_space_w_scaling)
extern PFN_vkCmdSetViewportWScalingNV vkCmdSetViewportWScalingNV;
#endif /* defined(VK_NV_clip_space_w_scaling) */
#if defined(VK_NV_device_diagnostic_checkpoints)
extern PFN_vkCmdSetCheckpointNV vkCmdSetCheckpointNV;
extern PFN_vkGetQueueCheckpointDataNV vkGetQueueCheckpointDataNV;
#endif /* defined(VK_NV_device_diagnostic_checkpoints) */
#if defined(VK_NV_external_memory_capabilities)
extern PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV vkGetPhysicalDeviceExternalImageFormatPropertiesNV;
#endif /* defined(VK_NV_external_memory_capabilities) */
#if defined(VK_NV_external_memory_win32)
extern PFN_vkGetMemoryWin32HandleNV vkGetMemoryWin32HandleNV;
#endif /* defined(VK_NV_external_memory_win32) */
#if defined(VK_NV_mesh_shader)
extern PFN_vkCmdDrawMeshTasksIndirectCountNV vkCmdDrawMeshTasksIndirectCountNV;
extern PFN_vkCmdDrawMeshTasksIndirectNV vkCmdDrawMeshTasksIndirectNV;
extern PFN_vkCmdDrawMeshTasksNV vkCmdDrawMeshTasksNV;
#endif /* defined(VK_NV_mesh_shader) */
#if defined(VK_NV_ray_tracing)
extern PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV;
extern PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV;
extern PFN_vkCmdCopyAccelerationStructureNV vkCmdCopyAccelerationStructureNV;
extern PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV;
extern PFN_vkCmdWriteAccelerationStructuresPropertiesNV vkCmdWriteAccelerationStructuresPropertiesNV;
extern PFN_vkCompileDeferredNV vkCompileDeferredNV;
extern PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV;
extern PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV;
extern PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV;
extern PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV;
extern PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV;
extern PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV;
#endif /* defined(VK_NV_ray_tracing) */
#if defined(VK_NV_scissor_exclusive)
extern PFN_vkCmdSetExclusiveScissorNV vkCmdSetExclusiveScissorNV;
#endif /* defined(VK_NV_scissor_exclusive) */
#if defined(VK_NV_shading_rate_image)
extern PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV;
extern PFN_vkCmdSetCoarseSampleOrderNV vkCmdSetCoarseSampleOrderNV;
extern PFN_vkCmdSetViewportShadingRatePaletteNV vkCmdSetViewportShadingRatePaletteNV;
#endif /* defined(VK_NV_shading_rate_image) */
#if (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1))
extern PFN_vkCmdPushDescriptorSetWithTemplateKHR vkCmdPushDescriptorSetWithTemplateKHR;
#endif /* (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
extern PFN_vkGetDeviceGroupPresentCapabilitiesKHR vkGetDeviceGroupPresentCapabilitiesKHR;
extern PFN_vkGetDeviceGroupSurfacePresentModesKHR vkGetDeviceGroupSurfacePresentModesKHR;
extern PFN_vkGetPhysicalDevicePresentRectanglesKHR vkGetPhysicalDevicePresentRectanglesKHR;
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
extern PFN_vkAcquireNextImage2KHR vkAcquireNextImage2KHR;
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
/* VOLK_GENERATE_PROTOTYPES_H */

static PFN_vkVoidFunction vkGetInstanceProcAddrStub(void* context, const char* name)
{
	return vkGetInstanceProcAddr((VkInstance)context, name);
}

static void volkGenLoadLoader(void* context, PFN_vkVoidFunction (*load)(void*, const char*))
{
	/* VOLK_GENERATE_LOAD_LOADER */
#if defined(VK_VERSION_1_0)
	vkCreateInstance = (PFN_vkCreateInstance)load(context, "vkCreateInstance");
	vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)load(context, "vkEnumerateInstanceExtensionProperties");
	vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)load(context, "vkEnumerateInstanceLayerProperties");
#endif /* defined(VK_VERSION_1_0) */
#if defined(VK_VERSION_1_1)
	vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)load(context, "vkEnumerateInstanceVersion");
#endif /* defined(VK_VERSION_1_1) */
	/* VOLK_GENERATE_LOAD_LOADER */
}

static void volkGenLoadInstance(void* context, PFN_vkVoidFunction (*load)(void*, const char*))
{
	/* VOLK_GENERATE_LOAD_INSTANCE */
#if defined(VK_VERSION_1_0)
	vkCreateDevice = (PFN_vkCreateDevice)load(context, "vkCreateDevice");
	vkDestroyInstance = (PFN_vkDestroyInstance)load(context, "vkDestroyInstance");
	vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)load(context, "vkEnumerateDeviceExtensionProperties");
	vkEnumerateDeviceLayerProperties = (PFN_vkEnumerateDeviceLayerProperties)load(context, "vkEnumerateDeviceLayerProperties");
	vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)load(context, "vkEnumeratePhysicalDevices");
	vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)load(context, "vkGetDeviceProcAddr");
	vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)load(context, "vkGetPhysicalDeviceFeatures");
	vkGetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)load(context, "vkGetPhysicalDeviceFormatProperties");
	vkGetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)load(context, "vkGetPhysicalDeviceImageFormatProperties");
	vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)load(context, "vkGetPhysicalDeviceMemoryProperties");
	vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)load(context, "vkGetPhysicalDeviceProperties");
	vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)load(context, "vkGetPhysicalDeviceQueueFamilyProperties");
	vkGetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)load(context, "vkGetPhysicalDeviceSparseImageFormatProperties");
#endif /* defined(VK_VERSION_1_0) */
#if defined(VK_VERSION_1_1)
	vkEnumeratePhysicalDeviceGroups = (PFN_vkEnumeratePhysicalDeviceGroups)load(context, "vkEnumeratePhysicalDeviceGroups");
	vkGetPhysicalDeviceExternalBufferProperties = (PFN_vkGetPhysicalDeviceExternalBufferProperties)load(context, "vkGetPhysicalDeviceExternalBufferProperties");
	vkGetPhysicalDeviceExternalFenceProperties = (PFN_vkGetPhysicalDeviceExternalFenceProperties)load(context, "vkGetPhysicalDeviceExternalFenceProperties");
	vkGetPhysicalDeviceExternalSemaphoreProperties = (PFN_vkGetPhysicalDeviceExternalSemaphoreProperties)load(context, "vkGetPhysicalDeviceExternalSemaphoreProperties");
	vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2)load(context, "vkGetPhysicalDeviceFeatures2");
	vkGetPhysicalDeviceFormatProperties2 = (PFN_vkGetPhysicalDeviceFormatProperties2)load(context, "vkGetPhysicalDeviceFormatProperties2");
	vkGetPhysicalDeviceImageFormatProperties2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2)load(context, "vkGetPhysicalDeviceImageFormatProperties2");
	vkGetPhysicalDeviceMemoryProperties2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)load(context, "vkGetPhysicalDeviceMemoryProperties2");
	vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)load(context, "vkGetPhysicalDeviceProperties2");
	vkGetPhysicalDeviceQueueFamilyProperties2 = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2)load(context, "vkGetPhysicalDeviceQueueFamilyProperties2");
	vkGetPhysicalDeviceSparseImageFormatProperties2 = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2)load(context, "vkGetPhysicalDeviceSparseImageFormatProperties2");
#endif /* defined(VK_VERSION_1_1) */
#if defined(VK_EXT_acquire_xlib_display)
	vkAcquireXlibDisplayEXT = (PFN_vkAcquireXlibDisplayEXT)load(context, "vkAcquireXlibDisplayEXT");
	vkGetRandROutputDisplayEXT = (PFN_vkGetRandROutputDisplayEXT)load(context, "vkGetRandROutputDisplayEXT");
#endif /* defined(VK_EXT_acquire_xlib_display) */
#if defined(VK_EXT_calibrated_timestamps)
	vkGetPhysicalDeviceCalibrateableTimeDomainsEXT = (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)load(context, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
#endif /* defined(VK_EXT_calibrated_timestamps) */
#if defined(VK_EXT_debug_report)
	vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)load(context, "vkCreateDebugReportCallbackEXT");
	vkDebugReportMessageEXT = (PFN_vkDebugReportMessageEXT)load(context, "vkDebugReportMessageEXT");
	vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)load(context, "vkDestroyDebugReportCallbackEXT");
#endif /* defined(VK_EXT_debug_report) */
#if defined(VK_EXT_debug_utils)
	vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)load(context, "vkCreateDebugUtilsMessengerEXT");
	vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)load(context, "vkDestroyDebugUtilsMessengerEXT");
	vkSubmitDebugUtilsMessageEXT = (PFN_vkSubmitDebugUtilsMessageEXT)load(context, "vkSubmitDebugUtilsMessageEXT");
#endif /* defined(VK_EXT_debug_utils) */
#if defined(VK_EXT_direct_mode_display)
	vkReleaseDisplayEXT = (PFN_vkReleaseDisplayEXT)load(context, "vkReleaseDisplayEXT");
#endif /* defined(VK_EXT_direct_mode_display) */
#if defined(VK_EXT_display_surface_counter)
	vkGetPhysicalDeviceSurfaceCapabilities2EXT = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT)load(context, "vkGetPhysicalDeviceSurfaceCapabilities2EXT");
#endif /* defined(VK_EXT_display_surface_counter) */
#if defined(VK_EXT_sample_locations)
	vkGetPhysicalDeviceMultisamplePropertiesEXT = (PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT)load(context, "vkGetPhysicalDeviceMultisamplePropertiesEXT");
#endif /* defined(VK_EXT_sample_locations) */
#if defined(VK_FUCHSIA_imagepipe_surface)
	vkCreateImagePipeSurfaceFUCHSIA = (PFN_vkCreateImagePipeSurfaceFUCHSIA)load(context, "vkCreateImagePipeSurfaceFUCHSIA");
#endif /* defined(VK_FUCHSIA_imagepipe_surface) */
#if defined(VK_KHR_android_surface)
	vkCreateAndroidSurfaceKHR = (PFN_vkCreateAndroidSurfaceKHR)load(context, "vkCreateAndroidSurfaceKHR");
#endif /* defined(VK_KHR_android_surface) */
#if defined(VK_KHR_device_group_creation)
	vkEnumeratePhysicalDeviceGroupsKHR = (PFN_vkEnumeratePhysicalDeviceGroupsKHR)load(context, "vkEnumeratePhysicalDeviceGroupsKHR");
#endif /* defined(VK_KHR_device_group_creation) */
#if defined(VK_KHR_display)
	vkCreateDisplayModeKHR = (PFN_vkCreateDisplayModeKHR)load(context, "vkCreateDisplayModeKHR");
	vkCreateDisplayPlaneSurfaceKHR = (PFN_vkCreateDisplayPlaneSurfaceKHR)load(context, "vkCreateDisplayPlaneSurfaceKHR");
	vkGetDisplayModePropertiesKHR = (PFN_vkGetDisplayModePropertiesKHR)load(context, "vkGetDisplayModePropertiesKHR");
	vkGetDisplayPlaneCapabilitiesKHR = (PFN_vkGetDisplayPlaneCapabilitiesKHR)load(context, "vkGetDisplayPlaneCapabilitiesKHR");
	vkGetDisplayPlaneSupportedDisplaysKHR = (PFN_vkGetDisplayPlaneSupportedDisplaysKHR)load(context, "vkGetDisplayPlaneSupportedDisplaysKHR");
	vkGetPhysicalDeviceDisplayPlanePropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)load(context, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
	vkGetPhysicalDeviceDisplayPropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)load(context, "vkGetPhysicalDeviceDisplayPropertiesKHR");
#endif /* defined(VK_KHR_display) */
#if defined(VK_KHR_external_fence_capabilities)
	vkGetPhysicalDeviceExternalFencePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR)load(context, "vkGetPhysicalDeviceExternalFencePropertiesKHR");
#endif /* defined(VK_KHR_external_fence_capabilities) */
#if defined(VK_KHR_external_memory_capabilities)
	vkGetPhysicalDeviceExternalBufferPropertiesKHR = (PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR)load(context, "vkGetPhysicalDeviceExternalBufferPropertiesKHR");
#endif /* defined(VK_KHR_external_memory_capabilities) */
#if defined(VK_KHR_external_semaphore_capabilities)
	vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR)load(context, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");
#endif /* defined(VK_KHR_external_semaphore_capabilities) */
#if defined(VK_KHR_get_display_properties2)
	vkGetDisplayModeProperties2KHR = (PFN_vkGetDisplayModeProperties2KHR)load(context, "vkGetDisplayModeProperties2KHR");
	vkGetDisplayPlaneCapabilities2KHR = (PFN_vkGetDisplayPlaneCapabilities2KHR)load(context, "vkGetDisplayPlaneCapabilities2KHR");
	vkGetPhysicalDeviceDisplayPlaneProperties2KHR = (PFN_vkGetPhysicalDeviceDisplayPlaneProperties2KHR)load(context, "vkGetPhysicalDeviceDisplayPlaneProperties2KHR");
	vkGetPhysicalDeviceDisplayProperties2KHR = (PFN_vkGetPhysicalDeviceDisplayProperties2KHR)load(context, "vkGetPhysicalDeviceDisplayProperties2KHR");
#endif /* defined(VK_KHR_get_display_properties2) */
#if defined(VK_KHR_get_physical_device_properties2)
	vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)load(context, "vkGetPhysicalDeviceFeatures2KHR");
	vkGetPhysicalDeviceFormatProperties2KHR = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)load(context, "vkGetPhysicalDeviceFormatProperties2KHR");
	vkGetPhysicalDeviceImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)load(context, "vkGetPhysicalDeviceImageFormatProperties2KHR");
	vkGetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR)load(context, "vkGetPhysicalDeviceMemoryProperties2KHR");
	vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)load(context, "vkGetPhysicalDeviceProperties2KHR");
	vkGetPhysicalDeviceQueueFamilyProperties2KHR = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR)load(context, "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
	vkGetPhysicalDeviceSparseImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR)load(context, "vkGetPhysicalDeviceSparseImageFormatProperties2KHR");
#endif /* defined(VK_KHR_get_physical_device_properties2) */
#if defined(VK_KHR_get_surface_capabilities2)
	vkGetPhysicalDeviceSurfaceCapabilities2KHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)load(context, "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
	vkGetPhysicalDeviceSurfaceFormats2KHR = (PFN_vkGetPhysicalDeviceSurfaceFormats2KHR)load(context, "vkGetPhysicalDeviceSurfaceFormats2KHR");
#endif /* defined(VK_KHR_get_surface_capabilities2) */
#if defined(VK_KHR_surface)
	vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)load(context, "vkDestroySurfaceKHR");
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)load(context, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)load(context, "vkGetPhysicalDeviceSurfaceFormatsKHR");
	vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)load(context, "vkGetPhysicalDeviceSurfacePresentModesKHR");
	vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)load(context, "vkGetPhysicalDeviceSurfaceSupportKHR");
#endif /* defined(VK_KHR_surface) */
#if defined(VK_KHR_wayland_surface)
	vkCreateWaylandSurfaceKHR = (PFN_vkCreateWaylandSurfaceKHR)load(context, "vkCreateWaylandSurfaceKHR");
	vkGetPhysicalDeviceWaylandPresentationSupportKHR = (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)load(context, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
#endif /* defined(VK_KHR_wayland_surface) */
#if defined(VK_KHR_win32_surface)
	vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)load(context, "vkCreateWin32SurfaceKHR");
	vkGetPhysicalDeviceWin32PresentationSupportKHR = (PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)load(context, "vkGetPhysicalDeviceWin32PresentationSupportKHR");
#endif /* defined(VK_KHR_win32_surface) */
#if defined(VK_KHR_xcb_surface)
	vkCreateXcbSurfaceKHR = (PFN_vkCreateXcbSurfaceKHR)load(context, "vkCreateXcbSurfaceKHR");
	vkGetPhysicalDeviceXcbPresentationSupportKHR = (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)load(context, "vkGetPhysicalDeviceXcbPresentationSupportKHR");
#endif /* defined(VK_KHR_xcb_surface) */
#if defined(VK_KHR_xlib_surface)
	vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)load(context, "vkCreateXlibSurfaceKHR");
	vkGetPhysicalDeviceXlibPresentationSupportKHR = (PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)load(context, "vkGetPhysicalDeviceXlibPresentationSupportKHR");
#endif /* defined(VK_KHR_xlib_surface) */
#if defined(VK_MVK_ios_surface)
	vkCreateIOSSurfaceMVK = (PFN_vkCreateIOSSurfaceMVK)load(context, "vkCreateIOSSurfaceMVK");
#endif /* defined(VK_MVK_ios_surface) */
#if defined(VK_MVK_macos_surface)
	vkCreateMacOSSurfaceMVK = (PFN_vkCreateMacOSSurfaceMVK)load(context, "vkCreateMacOSSurfaceMVK");
#endif /* defined(VK_MVK_macos_surface) */
#if defined(VK_NN_vi_surface)
	vkCreateViSurfaceNN = (PFN_vkCreateViSurfaceNN)load(context, "vkCreateViSurfaceNN");
#endif /* defined(VK_NN_vi_surface) */
#if defined(VK_NVX_device_generated_commands)
	vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX = (PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX)load(context, "vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX");
#endif /* defined(VK_NVX_device_generated_commands) */
#if defined(VK_NV_external_memory_capabilities)
	vkGetPhysicalDeviceExternalImageFormatPropertiesNV = (PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV)load(context, "vkGetPhysicalDeviceExternalImageFormatPropertiesNV");
#endif /* defined(VK_NV_external_memory_capabilities) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
	vkGetPhysicalDevicePresentRectanglesKHR = (PFN_vkGetPhysicalDevicePresentRectanglesKHR)load(context, "vkGetPhysicalDevicePresentRectanglesKHR");
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
	/* VOLK_GENERATE_LOAD_INSTANCE */
}

static void volkGenLoadDevice(void* context, PFN_vkVoidFunction (*load)(void*, const char*))
{
	/* VOLK_GENERATE_LOAD_DEVICE */
#if defined(VK_VERSION_1_0)
	vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)load(context, "vkAllocateCommandBuffers");
	vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)load(context, "vkAllocateDescriptorSets");
	vkAllocateMemory = (PFN_vkAllocateMemory)load(context, "vkAllocateMemory");
	vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)load(context, "vkBeginCommandBuffer");
	vkBindBufferMemory = (PFN_vkBindBufferMemory)load(context, "vkBindBufferMemory");
	vkBindImageMemory = (PFN_vkBindImageMemory)load(context, "vkBindImageMemory");
	vkCmdBeginQuery = (PFN_vkCmdBeginQuery)load(context, "vkCmdBeginQuery");
	vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)load(context, "vkCmdBeginRenderPass");
	vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)load(context, "vkCmdBindDescriptorSets");
	vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)load(context, "vkCmdBindIndexBuffer");
	vkCmdBindPipeline = (PFN_vkCmdBindPipeline)load(context, "vkCmdBindPipeline");
	vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)load(context, "vkCmdBindVertexBuffers");
	vkCmdBlitImage = (PFN_vkCmdBlitImage)load(context, "vkCmdBlitImage");
	vkCmdClearAttachments = (PFN_vkCmdClearAttachments)load(context, "vkCmdClearAttachments");
	vkCmdClearColorImage = (PFN_vkCmdClearColorImage)load(context, "vkCmdClearColorImage");
	vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)load(context, "vkCmdClearDepthStencilImage");
	vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)load(context, "vkCmdCopyBuffer");
	vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)load(context, "vkCmdCopyBufferToImage");
	vkCmdCopyImage = (PFN_vkCmdCopyImage)load(context, "vkCmdCopyImage");
	vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)load(context, "vkCmdCopyImageToBuffer");
	vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)load(context, "vkCmdCopyQueryPoolResults");
	vkCmdDispatch = (PFN_vkCmdDispatch)load(context, "vkCmdDispatch");
	vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)load(context, "vkCmdDispatchIndirect");
	vkCmdDraw = (PFN_vkCmdDraw)load(context, "vkCmdDraw");
	vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)load(context, "vkCmdDrawIndexed");
	vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)load(context, "vkCmdDrawIndexedIndirect");
	vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect)load(context, "vkCmdDrawIndirect");
	vkCmdEndQuery = (PFN_vkCmdEndQuery)load(context, "vkCmdEndQuery");
	vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)load(context, "vkCmdEndRenderPass");
	vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)load(context, "vkCmdExecuteCommands");
	vkCmdFillBuffer = (PFN_vkCmdFillBuffer)load(context, "vkCmdFillBuffer");
	vkCmdNextSubpass = (PFN_vkCmdNextSubpass)load(context, "vkCmdNextSubpass");
	vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)load(context, "vkCmdPipelineBarrier");
	vkCmdPushConstants = (PFN_vkCmdPushConstants)load(context, "vkCmdPushConstants");
	vkCmdResetEvent = (PFN_vkCmdResetEvent)load(context, "vkCmdResetEvent");
	vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)load(context, "vkCmdResetQueryPool");
	vkCmdResolveImage = (PFN_vkCmdResolveImage)load(context, "vkCmdResolveImage");
	vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)load(context, "vkCmdSetBlendConstants");
	vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias)load(context, "vkCmdSetDepthBias");
	vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)load(context, "vkCmdSetDepthBounds");
	vkCmdSetEvent = (PFN_vkCmdSetEvent)load(context, "vkCmdSetEvent");
	vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)load(context, "vkCmdSetLineWidth");
	vkCmdSetScissor = (PFN_vkCmdSetScissor)load(context, "vkCmdSetScissor");
	vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)load(context, "vkCmdSetStencilCompareMask");
	vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)load(context, "vkCmdSetStencilReference");
	vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)load(context, "vkCmdSetStencilWriteMask");
	vkCmdSetViewport = (PFN_vkCmdSetViewport)load(context, "vkCmdSetViewport");
	vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)load(context, "vkCmdUpdateBuffer");
	vkCmdWaitEvents = (PFN_vkCmdWaitEvents)load(context, "vkCmdWaitEvents");
	vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)load(context, "vkCmdWriteTimestamp");
	vkCreateBuffer = (PFN_vkCreateBuffer)load(context, "vkCreateBuffer");
	vkCreateBufferView = (PFN_vkCreateBufferView)load(context, "vkCreateBufferView");
	vkCreateCommandPool = (PFN_vkCreateCommandPool)load(context, "vkCreateCommandPool");
	vkCreateComputePipelines = (PFN_vkCreateComputePipelines)load(context, "vkCreateComputePipelines");
	vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)load(context, "vkCreateDescriptorPool");
	vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)load(context, "vkCreateDescriptorSetLayout");
	vkCreateEvent = (PFN_vkCreateEvent)load(context, "vkCreateEvent");
	vkCreateFence = (PFN_vkCreateFence)load(context, "vkCreateFence");
	vkCreateFramebuffer = (PFN_vkCreateFramebuffer)load(context, "vkCreateFramebuffer");
	vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)load(context, "vkCreateGraphicsPipelines");
	vkCreateImage = (PFN_vkCreateImage)load(context, "vkCreateImage");
	vkCreateImageView = (PFN_vkCreateImageView)load(context, "vkCreateImageView");
	vkCreatePipelineCache = (PFN_vkCreatePipelineCache)load(context, "vkCreatePipelineCache");
	vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)load(context, "vkCreatePipelineLayout");
	vkCreateQueryPool = (PFN_vkCreateQueryPool)load(context, "vkCreateQueryPool");
	vkCreateRenderPass = (PFN_vkCreateRenderPass)load(context, "vkCreateRenderPass");
	vkCreateSampler = (PFN_vkCreateSampler)load(context, "vkCreateSampler");
	vkCreateSemaphore = (PFN_vkCreateSemaphore)load(context, "vkCreateSemaphore");
	vkCreateShaderModule = (PFN_vkCreateShaderModule)load(context, "vkCreateShaderModule");
	vkDestroyBuffer = (PFN_vkDestroyBuffer)load(context, "vkDestroyBuffer");
	vkDestroyBufferView = (PFN_vkDestroyBufferView)load(context, "vkDestroyBufferView");
	vkDestroyCommandPool = (PFN_vkDestroyCommandPool)load(context, "vkDestroyCommandPool");
	vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)load(context, "vkDestroyDescriptorPool");
	vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)load(context, "vkDestroyDescriptorSetLayout");
	vkDestroyDevice = (PFN_vkDestroyDevice)load(context, "vkDestroyDevice");
	vkDestroyEvent = (PFN_vkDestroyEvent)load(context, "vkDestroyEvent");
	vkDestroyFence = (PFN_vkDestroyFence)load(context, "vkDestroyFence");
	vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)load(context, "vkDestroyFramebuffer");
	vkDestroyImage = (PFN_vkDestroyImage)load(context, "vkDestroyImage");
	vkDestroyImageView = (PFN_vkDestroyImageView)load(context, "vkDestroyImageView");
	vkDestroyPipeline = (PFN_vkDestroyPipeline)load(context, "vkDestroyPipeline");
	vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)load(context, "vkDestroyPipelineCache");
	vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)load(context, "vkDestroyPipelineLayout");
	vkDestroyQueryPool = (PFN_vkDestroyQueryPool)load(context, "vkDestroyQueryPool");
	vkDestroyRenderPass = (PFN_vkDestroyRenderPass)load(context, "vkDestroyRenderPass");
	vkDestroySampler = (PFN_vkDestroySampler)load(context, "vkDestroySampler");
	vkDestroySemaphore = (PFN_vkDestroySemaphore)load(context, "vkDestroySemaphore");
	vkDestroyShaderModule = (PFN_vkDestroyShaderModule)load(context, "vkDestroyShaderModule");
	vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)load(context, "vkDeviceWaitIdle");
	vkEndCommandBuffer = (PFN_vkEndCommandBuffer)load(context, "vkEndCommandBuffer");
	vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)load(context, "vkFlushMappedMemoryRanges");
	vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)load(context, "vkFreeCommandBuffers");
	vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)load(context, "vkFreeDescriptorSets");
	vkFreeMemory = (PFN_vkFreeMemory)load(context, "vkFreeMemory");
	vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)load(context, "vkGetBufferMemoryRequirements");
	vkGetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)load(context, "vkGetDeviceMemoryCommitment");
	vkGetDeviceQueue = (PFN_vkGetDeviceQueue)load(context, "vkGetDeviceQueue");
	vkGetEventStatus = (PFN_vkGetEventStatus)load(context, "vkGetEventStatus");
	vkGetFenceStatus = (PFN_vkGetFenceStatus)load(context, "vkGetFenceStatus");
	vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)load(context, "vkGetImageMemoryRequirements");
	vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)load(context, "vkGetImageSparseMemoryRequirements");
	vkGetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)load(context, "vkGetImageSubresourceLayout");
	vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)load(context, "vkGetPipelineCacheData");
	vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)load(context, "vkGetQueryPoolResults");
	vkGetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)load(context, "vkGetRenderAreaGranularity");
	vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)load(context, "vkInvalidateMappedMemoryRanges");
	vkMapMemory = (PFN_vkMapMemory)load(context, "vkMapMemory");
	vkMergePipelineCaches = (PFN_vkMergePipelineCaches)load(context, "vkMergePipelineCaches");
	vkQueueBindSparse = (PFN_vkQueueBindSparse)load(context, "vkQueueBindSparse");
	vkQueueSubmit = (PFN_vkQueueSubmit)load(context, "vkQueueSubmit");
	vkQueueWaitIdle = (PFN_vkQueueWaitIdle)load(context, "vkQueueWaitIdle");
	vkResetCommandBuffer = (PFN_vkResetCommandBuffer)load(context, "vkResetCommandBuffer");
	vkResetCommandPool = (PFN_vkResetCommandPool)load(context, "vkResetCommandPool");
	vkResetDescriptorPool = (PFN_vkResetDescriptorPool)load(context, "vkResetDescriptorPool");
	vkResetEvent = (PFN_vkResetEvent)load(context, "vkResetEvent");
	vkResetFences = (PFN_vkResetFences)load(context, "vkResetFences");
	vkSetEvent = (PFN_vkSetEvent)load(context, "vkSetEvent");
	vkUnmapMemory = (PFN_vkUnmapMemory)load(context, "vkUnmapMemory");
	vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)load(context, "vkUpdateDescriptorSets");
	vkWaitForFences = (PFN_vkWaitForFences)load(context, "vkWaitForFences");
#endif /* defined(VK_VERSION_1_0) */
#if defined(VK_VERSION_1_1)
	vkBindBufferMemory2 = (PFN_vkBindBufferMemory2)load(context, "vkBindBufferMemory2");
	vkBindImageMemory2 = (PFN_vkBindImageMemory2)load(context, "vkBindImageMemory2");
	vkCmdDispatchBase = (PFN_vkCmdDispatchBase)load(context, "vkCmdDispatchBase");
	vkCmdSetDeviceMask = (PFN_vkCmdSetDeviceMask)load(context, "vkCmdSetDeviceMask");
	vkCreateDescriptorUpdateTemplate = (PFN_vkCreateDescriptorUpdateTemplate)load(context, "vkCreateDescriptorUpdateTemplate");
	vkCreateSamplerYcbcrConversion = (PFN_vkCreateSamplerYcbcrConversion)load(context, "vkCreateSamplerYcbcrConversion");
	vkDestroyDescriptorUpdateTemplate = (PFN_vkDestroyDescriptorUpdateTemplate)load(context, "vkDestroyDescriptorUpdateTemplate");
	vkDestroySamplerYcbcrConversion = (PFN_vkDestroySamplerYcbcrConversion)load(context, "vkDestroySamplerYcbcrConversion");
	vkGetBufferMemoryRequirements2 = (PFN_vkGetBufferMemoryRequirements2)load(context, "vkGetBufferMemoryRequirements2");
	vkGetDescriptorSetLayoutSupport = (PFN_vkGetDescriptorSetLayoutSupport)load(context, "vkGetDescriptorSetLayoutSupport");
	vkGetDeviceGroupPeerMemoryFeatures = (PFN_vkGetDeviceGroupPeerMemoryFeatures)load(context, "vkGetDeviceGroupPeerMemoryFeatures");
	vkGetDeviceQueue2 = (PFN_vkGetDeviceQueue2)load(context, "vkGetDeviceQueue2");
	vkGetImageMemoryRequirements2 = (PFN_vkGetImageMemoryRequirements2)load(context, "vkGetImageMemoryRequirements2");
	vkGetImageSparseMemoryRequirements2 = (PFN_vkGetImageSparseMemoryRequirements2)load(context, "vkGetImageSparseMemoryRequirements2");
	vkTrimCommandPool = (PFN_vkTrimCommandPool)load(context, "vkTrimCommandPool");
	vkUpdateDescriptorSetWithTemplate = (PFN_vkUpdateDescriptorSetWithTemplate)load(context, "vkUpdateDescriptorSetWithTemplate");
#endif /* defined(VK_VERSION_1_1) */
#if defined(VK_AMD_buffer_marker)
	vkCmdWriteBufferMarkerAMD = (PFN_vkCmdWriteBufferMarkerAMD)load(context, "vkCmdWriteBufferMarkerAMD");
#endif /* defined(VK_AMD_buffer_marker) */
#if defined(VK_AMD_draw_indirect_count)
	vkCmdDrawIndexedIndirectCountAMD = (PFN_vkCmdDrawIndexedIndirectCountAMD)load(context, "vkCmdDrawIndexedIndirectCountAMD");
	vkCmdDrawIndirectCountAMD = (PFN_vkCmdDrawIndirectCountAMD)load(context, "vkCmdDrawIndirectCountAMD");
#endif /* defined(VK_AMD_draw_indirect_count) */
#if defined(VK_AMD_shader_info)
	vkGetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)load(context, "vkGetShaderInfoAMD");
#endif /* defined(VK_AMD_shader_info) */
#if defined(VK_ANDROID_external_memory_android_hardware_buffer)
	vkGetAndroidHardwareBufferPropertiesANDROID = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)load(context, "vkGetAndroidHardwareBufferPropertiesANDROID");
	vkGetMemoryAndroidHardwareBufferANDROID = (PFN_vkGetMemoryAndroidHardwareBufferANDROID)load(context, "vkGetMemoryAndroidHardwareBufferANDROID");
#endif /* defined(VK_ANDROID_external_memory_android_hardware_buffer) */
#if defined(VK_EXT_calibrated_timestamps)
	vkGetCalibratedTimestampsEXT = (PFN_vkGetCalibratedTimestampsEXT)load(context, "vkGetCalibratedTimestampsEXT");
#endif /* defined(VK_EXT_calibrated_timestamps) */
#if defined(VK_EXT_conditional_rendering)
	vkCmdBeginConditionalRenderingEXT = (PFN_vkCmdBeginConditionalRenderingEXT)load(context, "vkCmdBeginConditionalRenderingEXT");
	vkCmdEndConditionalRenderingEXT = (PFN_vkCmdEndConditionalRenderingEXT)load(context, "vkCmdEndConditionalRenderingEXT");
#endif /* defined(VK_EXT_conditional_rendering) */
#if defined(VK_EXT_debug_marker)
	vkCmdDebugMarkerBeginEXT = (PFN_vkCmdDebugMarkerBeginEXT)load(context, "vkCmdDebugMarkerBeginEXT");
	vkCmdDebugMarkerEndEXT = (PFN_vkCmdDebugMarkerEndEXT)load(context, "vkCmdDebugMarkerEndEXT");
	vkCmdDebugMarkerInsertEXT = (PFN_vkCmdDebugMarkerInsertEXT)load(context, "vkCmdDebugMarkerInsertEXT");
	vkDebugMarkerSetObjectNameEXT = (PFN_vkDebugMarkerSetObjectNameEXT)load(context, "vkDebugMarkerSetObjectNameEXT");
	vkDebugMarkerSetObjectTagEXT = (PFN_vkDebugMarkerSetObjectTagEXT)load(context, "vkDebugMarkerSetObjectTagEXT");
#endif /* defined(VK_EXT_debug_marker) */
#if defined(VK_EXT_debug_utils)
	vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)load(context, "vkCmdBeginDebugUtilsLabelEXT");
	vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)load(context, "vkCmdEndDebugUtilsLabelEXT");
	vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)load(context, "vkCmdInsertDebugUtilsLabelEXT");
	vkQueueBeginDebugUtilsLabelEXT = (PFN_vkQueueBeginDebugUtilsLabelEXT)load(context, "vkQueueBeginDebugUtilsLabelEXT");
	vkQueueEndDebugUtilsLabelEXT = (PFN_vkQueueEndDebugUtilsLabelEXT)load(context, "vkQueueEndDebugUtilsLabelEXT");
	vkQueueInsertDebugUtilsLabelEXT = (PFN_vkQueueInsertDebugUtilsLabelEXT)load(context, "vkQueueInsertDebugUtilsLabelEXT");
	vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)load(context, "vkSetDebugUtilsObjectNameEXT");
	vkSetDebugUtilsObjectTagEXT = (PFN_vkSetDebugUtilsObjectTagEXT)load(context, "vkSetDebugUtilsObjectTagEXT");
#endif /* defined(VK_EXT_debug_utils) */
#if defined(VK_EXT_discard_rectangles)
	vkCmdSetDiscardRectangleEXT = (PFN_vkCmdSetDiscardRectangleEXT)load(context, "vkCmdSetDiscardRectangleEXT");
#endif /* defined(VK_EXT_discard_rectangles) */
#if defined(VK_EXT_display_control)
	vkDisplayPowerControlEXT = (PFN_vkDisplayPowerControlEXT)load(context, "vkDisplayPowerControlEXT");
	vkGetSwapchainCounterEXT = (PFN_vkGetSwapchainCounterEXT)load(context, "vkGetSwapchainCounterEXT");
	vkRegisterDeviceEventEXT = (PFN_vkRegisterDeviceEventEXT)load(context, "vkRegisterDeviceEventEXT");
	vkRegisterDisplayEventEXT = (PFN_vkRegisterDisplayEventEXT)load(context, "vkRegisterDisplayEventEXT");
#endif /* defined(VK_EXT_display_control) */
#if defined(VK_EXT_external_memory_host)
	vkGetMemoryHostPointerPropertiesEXT = (PFN_vkGetMemoryHostPointerPropertiesEXT)load(context, "vkGetMemoryHostPointerPropertiesEXT");
#endif /* defined(VK_EXT_external_memory_host) */
#if defined(VK_EXT_hdr_metadata)
	vkSetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT)load(context, "vkSetHdrMetadataEXT");
#endif /* defined(VK_EXT_hdr_metadata) */
#if defined(VK_EXT_image_drm_format_modifier)
	vkGetImageDrmFormatModifierPropertiesEXT = (PFN_vkGetImageDrmFormatModifierPropertiesEXT)load(context, "vkGetImageDrmFormatModifierPropertiesEXT");
#endif /* defined(VK_EXT_image_drm_format_modifier) */
#if defined(VK_EXT_sample_locations)
	vkCmdSetSampleLocationsEXT = (PFN_vkCmdSetSampleLocationsEXT)load(context, "vkCmdSetSampleLocationsEXT");
#endif /* defined(VK_EXT_sample_locations) */
#if defined(VK_EXT_transform_feedback)
	vkCmdBeginQueryIndexedEXT = (PFN_vkCmdBeginQueryIndexedEXT)load(context, "vkCmdBeginQueryIndexedEXT");
	vkCmdBeginTransformFeedbackEXT = (PFN_vkCmdBeginTransformFeedbackEXT)load(context, "vkCmdBeginTransformFeedbackEXT");
	vkCmdBindTransformFeedbackBuffersEXT = (PFN_vkCmdBindTransformFeedbackBuffersEXT)load(context, "vkCmdBindTransformFeedbackBuffersEXT");
	vkCmdDrawIndirectByteCountEXT = (PFN_vkCmdDrawIndirectByteCountEXT)load(context, "vkCmdDrawIndirectByteCountEXT");
	vkCmdEndQueryIndexedEXT = (PFN_vkCmdEndQueryIndexedEXT)load(context, "vkCmdEndQueryIndexedEXT");
	vkCmdEndTransformFeedbackEXT = (PFN_vkCmdEndTransformFeedbackEXT)load(context, "vkCmdEndTransformFeedbackEXT");
#endif /* defined(VK_EXT_transform_feedback) */
#if defined(VK_EXT_validation_cache)
	vkCreateValidationCacheEXT = (PFN_vkCreateValidationCacheEXT)load(context, "vkCreateValidationCacheEXT");
	vkDestroyValidationCacheEXT = (PFN_vkDestroyValidationCacheEXT)load(context, "vkDestroyValidationCacheEXT");
	vkGetValidationCacheDataEXT = (PFN_vkGetValidationCacheDataEXT)load(context, "vkGetValidationCacheDataEXT");
	vkMergeValidationCachesEXT = (PFN_vkMergeValidationCachesEXT)load(context, "vkMergeValidationCachesEXT");
#endif /* defined(VK_EXT_validation_cache) */
#if defined(VK_GOOGLE_display_timing)
	vkGetPastPresentationTimingGOOGLE = (PFN_vkGetPastPresentationTimingGOOGLE)load(context, "vkGetPastPresentationTimingGOOGLE");
	vkGetRefreshCycleDurationGOOGLE = (PFN_vkGetRefreshCycleDurationGOOGLE)load(context, "vkGetRefreshCycleDurationGOOGLE");
#endif /* defined(VK_GOOGLE_display_timing) */
#if defined(VK_KHR_bind_memory2)
	vkBindBufferMemory2KHR = (PFN_vkBindBufferMemory2KHR)load(context, "vkBindBufferMemory2KHR");
	vkBindImageMemory2KHR = (PFN_vkBindImageMemory2KHR)load(context, "vkBindImageMemory2KHR");
#endif /* defined(VK_KHR_bind_memory2) */
#if defined(VK_KHR_create_renderpass2)
	vkCmdBeginRenderPass2KHR = (PFN_vkCmdBeginRenderPass2KHR)load(context, "vkCmdBeginRenderPass2KHR");
	vkCmdEndRenderPass2KHR = (PFN_vkCmdEndRenderPass2KHR)load(context, "vkCmdEndRenderPass2KHR");
	vkCmdNextSubpass2KHR = (PFN_vkCmdNextSubpass2KHR)load(context, "vkCmdNextSubpass2KHR");
	vkCreateRenderPass2KHR = (PFN_vkCreateRenderPass2KHR)load(context, "vkCreateRenderPass2KHR");
#endif /* defined(VK_KHR_create_renderpass2) */
#if defined(VK_KHR_descriptor_update_template)
	vkCreateDescriptorUpdateTemplateKHR = (PFN_vkCreateDescriptorUpdateTemplateKHR)load(context, "vkCreateDescriptorUpdateTemplateKHR");
	vkDestroyDescriptorUpdateTemplateKHR = (PFN_vkDestroyDescriptorUpdateTemplateKHR)load(context, "vkDestroyDescriptorUpdateTemplateKHR");
	vkUpdateDescriptorSetWithTemplateKHR = (PFN_vkUpdateDescriptorSetWithTemplateKHR)load(context, "vkUpdateDescriptorSetWithTemplateKHR");
#endif /* defined(VK_KHR_descriptor_update_template) */
#if defined(VK_KHR_device_group)
	vkCmdDispatchBaseKHR = (PFN_vkCmdDispatchBaseKHR)load(context, "vkCmdDispatchBaseKHR");
	vkCmdSetDeviceMaskKHR = (PFN_vkCmdSetDeviceMaskKHR)load(context, "vkCmdSetDeviceMaskKHR");
	vkGetDeviceGroupPeerMemoryFeaturesKHR = (PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR)load(context, "vkGetDeviceGroupPeerMemoryFeaturesKHR");
#endif /* defined(VK_KHR_device_group) */
#if defined(VK_KHR_display_swapchain)
	vkCreateSharedSwapchainsKHR = (PFN_vkCreateSharedSwapchainsKHR)load(context, "vkCreateSharedSwapchainsKHR");
#endif /* defined(VK_KHR_display_swapchain) */
#if defined(VK_KHR_draw_indirect_count)
	vkCmdDrawIndexedIndirectCountKHR = (PFN_vkCmdDrawIndexedIndirectCountKHR)load(context, "vkCmdDrawIndexedIndirectCountKHR");
	vkCmdDrawIndirectCountKHR = (PFN_vkCmdDrawIndirectCountKHR)load(context, "vkCmdDrawIndirectCountKHR");
#endif /* defined(VK_KHR_draw_indirect_count) */
#if defined(VK_KHR_external_fence_fd)
	vkGetFenceFdKHR = (PFN_vkGetFenceFdKHR)load(context, "vkGetFenceFdKHR");
	vkImportFenceFdKHR = (PFN_vkImportFenceFdKHR)load(context, "vkImportFenceFdKHR");
#endif /* defined(VK_KHR_external_fence_fd) */
#if defined(VK_KHR_external_fence_win32)
	vkGetFenceWin32HandleKHR = (PFN_vkGetFenceWin32HandleKHR)load(context, "vkGetFenceWin32HandleKHR");
	vkImportFenceWin32HandleKHR = (PFN_vkImportFenceWin32HandleKHR)load(context, "vkImportFenceWin32HandleKHR");
#endif /* defined(VK_KHR_external_fence_win32) */
#if defined(VK_KHR_external_memory_fd)
	vkGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)load(context, "vkGetMemoryFdKHR");
	vkGetMemoryFdPropertiesKHR = (PFN_vkGetMemoryFdPropertiesKHR)load(context, "vkGetMemoryFdPropertiesKHR");
#endif /* defined(VK_KHR_external_memory_fd) */
#if defined(VK_KHR_external_memory_win32)
	vkGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)load(context, "vkGetMemoryWin32HandleKHR");
	vkGetMemoryWin32HandlePropertiesKHR = (PFN_vkGetMemoryWin32HandlePropertiesKHR)load(context, "vkGetMemoryWin32HandlePropertiesKHR");
#endif /* defined(VK_KHR_external_memory_win32) */
#if defined(VK_KHR_external_semaphore_fd)
	vkGetSemaphoreFdKHR = (PFN_vkGetSemaphoreFdKHR)load(context, "vkGetSemaphoreFdKHR");
	vkImportSemaphoreFdKHR = (PFN_vkImportSemaphoreFdKHR)load(context, "vkImportSemaphoreFdKHR");
#endif /* defined(VK_KHR_external_semaphore_fd) */
#if defined(VK_KHR_external_semaphore_win32)
	vkGetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)load(context, "vkGetSemaphoreWin32HandleKHR");
	vkImportSemaphoreWin32HandleKHR = (PFN_vkImportSemaphoreWin32HandleKHR)load(context, "vkImportSemaphoreWin32HandleKHR");
#endif /* defined(VK_KHR_external_semaphore_win32) */
#if defined(VK_KHR_get_memory_requirements2)
	vkGetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)load(context, "vkGetBufferMemoryRequirements2KHR");
	vkGetImageMemoryRequirements2KHR = (PFN_vkGetImageMemoryRequirements2KHR)load(context, "vkGetImageMemoryRequirements2KHR");
	vkGetImageSparseMemoryRequirements2KHR = (PFN_vkGetImageSparseMemoryRequirements2KHR)load(context, "vkGetImageSparseMemoryRequirements2KHR");
#endif /* defined(VK_KHR_get_memory_requirements2) */
#if defined(VK_KHR_maintenance1)
	vkTrimCommandPoolKHR = (PFN_vkTrimCommandPoolKHR)load(context, "vkTrimCommandPoolKHR");
#endif /* defined(VK_KHR_maintenance1) */
#if defined(VK_KHR_maintenance3)
	vkGetDescriptorSetLayoutSupportKHR = (PFN_vkGetDescriptorSetLayoutSupportKHR)load(context, "vkGetDescriptorSetLayoutSupportKHR");
#endif /* defined(VK_KHR_maintenance3) */
#if defined(VK_KHR_push_descriptor)
	vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)load(context, "vkCmdPushDescriptorSetKHR");
#endif /* defined(VK_KHR_push_descriptor) */
#if defined(VK_KHR_sampler_ycbcr_conversion)
	vkCreateSamplerYcbcrConversionKHR = (PFN_vkCreateSamplerYcbcrConversionKHR)load(context, "vkCreateSamplerYcbcrConversionKHR");
	vkDestroySamplerYcbcrConversionKHR = (PFN_vkDestroySamplerYcbcrConversionKHR)load(context, "vkDestroySamplerYcbcrConversionKHR");
#endif /* defined(VK_KHR_sampler_ycbcr_conversion) */
#if defined(VK_KHR_shared_presentable_image)
	vkGetSwapchainStatusKHR = (PFN_vkGetSwapchainStatusKHR)load(context, "vkGetSwapchainStatusKHR");
#endif /* defined(VK_KHR_shared_presentable_image) */
#if defined(VK_KHR_swapchain)
	vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)load(context, "vkAcquireNextImageKHR");
	vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)load(context, "vkCreateSwapchainKHR");
	vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)load(context, "vkDestroySwapchainKHR");
	vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)load(context, "vkGetSwapchainImagesKHR");
	vkQueuePresentKHR = (PFN_vkQueuePresentKHR)load(context, "vkQueuePresentKHR");
#endif /* defined(VK_KHR_swapchain) */
#if defined(VK_NVX_device_generated_commands)
	vkCmdProcessCommandsNVX = (PFN_vkCmdProcessCommandsNVX)load(context, "vkCmdProcessCommandsNVX");
	vkCmdReserveSpaceForCommandsNVX = (PFN_vkCmdReserveSpaceForCommandsNVX)load(context, "vkCmdReserveSpaceForCommandsNVX");
	vkCreateIndirectCommandsLayoutNVX = (PFN_vkCreateIndirectCommandsLayoutNVX)load(context, "vkCreateIndirectCommandsLayoutNVX");
	vkCreateObjectTableNVX = (PFN_vkCreateObjectTableNVX)load(context, "vkCreateObjectTableNVX");
	vkDestroyIndirectCommandsLayoutNVX = (PFN_vkDestroyIndirectCommandsLayoutNVX)load(context, "vkDestroyIndirectCommandsLayoutNVX");
	vkDestroyObjectTableNVX = (PFN_vkDestroyObjectTableNVX)load(context, "vkDestroyObjectTableNVX");
	vkRegisterObjectsNVX = (PFN_vkRegisterObjectsNVX)load(context, "vkRegisterObjectsNVX");
	vkUnregisterObjectsNVX = (PFN_vkUnregisterObjectsNVX)load(context, "vkUnregisterObjectsNVX");
#endif /* defined(VK_NVX_device_generated_commands) */
#if defined(VK_NVX_raytracing)
	vkBindAccelerationStructureMemoryNVX = (PFN_vkBindAccelerationStructureMemoryNVX)load(context, "vkBindAccelerationStructureMemoryNVX");
	vkCmdBuildAccelerationStructureNVX = (PFN_vkCmdBuildAccelerationStructureNVX)load(context, "vkCmdBuildAccelerationStructureNVX");
	vkCmdCopyAccelerationStructureNVX = (PFN_vkCmdCopyAccelerationStructureNVX)load(context, "vkCmdCopyAccelerationStructureNVX");
	vkCmdTraceRaysNVX = (PFN_vkCmdTraceRaysNVX)load(context, "vkCmdTraceRaysNVX");
	vkCmdWriteAccelerationStructurePropertiesNVX = (PFN_vkCmdWriteAccelerationStructurePropertiesNVX)load(context, "vkCmdWriteAccelerationStructurePropertiesNVX");
	vkCompileDeferredNVX = (PFN_vkCompileDeferredNVX)load(context, "vkCompileDeferredNVX");
	vkCreateAccelerationStructureNVX = (PFN_vkCreateAccelerationStructureNVX)load(context, "vkCreateAccelerationStructureNVX");
	vkCreateRaytracingPipelinesNVX = (PFN_vkCreateRaytracingPipelinesNVX)load(context, "vkCreateRaytracingPipelinesNVX");
	vkDestroyAccelerationStructureNVX = (PFN_vkDestroyAccelerationStructureNVX)load(context, "vkDestroyAccelerationStructureNVX");
	vkGetAccelerationStructureHandleNVX = (PFN_vkGetAccelerationStructureHandleNVX)load(context, "vkGetAccelerationStructureHandleNVX");
	vkGetAccelerationStructureMemoryRequirementsNVX = (PFN_vkGetAccelerationStructureMemoryRequirementsNVX)load(context, "vkGetAccelerationStructureMemoryRequirementsNVX");
	vkGetAccelerationStructureScratchMemoryRequirementsNVX = (PFN_vkGetAccelerationStructureScratchMemoryRequirementsNVX)load(context, "vkGetAccelerationStructureScratchMemoryRequirementsNVX");
	vkGetRaytracingShaderHandlesNVX = (PFN_vkGetRaytracingShaderHandlesNVX)load(context, "vkGetRaytracingShaderHandlesNVX");
#endif /* defined(VK_NVX_raytracing) */
#if defined(VK_NV_clip_space_w_scaling)
	vkCmdSetViewportWScalingNV = (PFN_vkCmdSetViewportWScalingNV)load(context, "vkCmdSetViewportWScalingNV");
#endif /* defined(VK_NV_clip_space_w_scaling) */
#if defined(VK_NV_device_diagnostic_checkpoints)
	vkCmdSetCheckpointNV = (PFN_vkCmdSetCheckpointNV)load(context, "vkCmdSetCheckpointNV");
	vkGetQueueCheckpointDataNV = (PFN_vkGetQueueCheckpointDataNV)load(context, "vkGetQueueCheckpointDataNV");
#endif /* defined(VK_NV_device_diagnostic_checkpoints) */
#if defined(VK_NV_external_memory_win32)
	vkGetMemoryWin32HandleNV = (PFN_vkGetMemoryWin32HandleNV)load(context, "vkGetMemoryWin32HandleNV");
#endif /* defined(VK_NV_external_memory_win32) */
#if defined(VK_NV_mesh_shader)
	vkCmdDrawMeshTasksIndirectCountNV = (PFN_vkCmdDrawMeshTasksIndirectCountNV)load(context, "vkCmdDrawMeshTasksIndirectCountNV");
	vkCmdDrawMeshTasksIndirectNV = (PFN_vkCmdDrawMeshTasksIndirectNV)load(context, "vkCmdDrawMeshTasksIndirectNV");
	vkCmdDrawMeshTasksNV = (PFN_vkCmdDrawMeshTasksNV)load(context, "vkCmdDrawMeshTasksNV");
#endif /* defined(VK_NV_mesh_shader) */
#if defined(VK_NV_ray_tracing)
	vkBindAccelerationStructureMemoryNV = (PFN_vkBindAccelerationStructureMemoryNV)load(context, "vkBindAccelerationStructureMemoryNV");
	vkCmdBuildAccelerationStructureNV = (PFN_vkCmdBuildAccelerationStructureNV)load(context, "vkCmdBuildAccelerationStructureNV");
	vkCmdCopyAccelerationStructureNV = (PFN_vkCmdCopyAccelerationStructureNV)load(context, "vkCmdCopyAccelerationStructureNV");
	vkCmdTraceRaysNV = (PFN_vkCmdTraceRaysNV)load(context, "vkCmdTraceRaysNV");
	vkCmdWriteAccelerationStructuresPropertiesNV = (PFN_vkCmdWriteAccelerationStructuresPropertiesNV)load(context, "vkCmdWriteAccelerationStructuresPropertiesNV");
	vkCompileDeferredNV = (PFN_vkCompileDeferredNV)load(context, "vkCompileDeferredNV");
	vkCreateAccelerationStructureNV = (PFN_vkCreateAccelerationStructureNV)load(context, "vkCreateAccelerationStructureNV");
	vkCreateRayTracingPipelinesNV = (PFN_vkCreateRayTracingPipelinesNV)load(context, "vkCreateRayTracingPipelinesNV");
	vkDestroyAccelerationStructureNV = (PFN_vkDestroyAccelerationStructureNV)load(context, "vkDestroyAccelerationStructureNV");
	vkGetAccelerationStructureHandleNV = (PFN_vkGetAccelerationStructureHandleNV)load(context, "vkGetAccelerationStructureHandleNV");
	vkGetAccelerationStructureMemoryRequirementsNV = (PFN_vkGetAccelerationStructureMemoryRequirementsNV)load(context, "vkGetAccelerationStructureMemoryRequirementsNV");
	vkGetRayTracingShaderGroupHandlesNV = (PFN_vkGetRayTracingShaderGroupHandlesNV)load(context, "vkGetRayTracingShaderGroupHandlesNV");
#endif /* defined(VK_NV_ray_tracing) */
#if defined(VK_NV_scissor_exclusive)
	vkCmdSetExclusiveScissorNV = (PFN_vkCmdSetExclusiveScissorNV)load(context, "vkCmdSetExclusiveScissorNV");
#endif /* defined(VK_NV_scissor_exclusive) */
#if defined(VK_NV_shading_rate_image)
	vkCmdBindShadingRateImageNV = (PFN_vkCmdBindShadingRateImageNV)load(context, "vkCmdBindShadingRateImageNV");
	vkCmdSetCoarseSampleOrderNV = (PFN_vkCmdSetCoarseSampleOrderNV)load(context, "vkCmdSetCoarseSampleOrderNV");
	vkCmdSetViewportShadingRatePaletteNV = (PFN_vkCmdSetViewportShadingRatePaletteNV)load(context, "vkCmdSetViewportShadingRatePaletteNV");
#endif /* defined(VK_NV_shading_rate_image) */
#if (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1))
	vkCmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)load(context, "vkCmdPushDescriptorSetWithTemplateKHR");
#endif /* (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
	vkGetDeviceGroupPresentCapabilitiesKHR = (PFN_vkGetDeviceGroupPresentCapabilitiesKHR)load(context, "vkGetDeviceGroupPresentCapabilitiesKHR");
	vkGetDeviceGroupSurfacePresentModesKHR = (PFN_vkGetDeviceGroupSurfacePresentModesKHR)load(context, "vkGetDeviceGroupSurfacePresentModesKHR");
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
	vkAcquireNextImage2KHR = (PFN_vkAcquireNextImage2KHR)load(context, "vkAcquireNextImage2KHR");
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
	/* VOLK_GENERATE_LOAD_DEVICE */
}

static PFN_vkVoidFunction vkGetDeviceProcAddrStub(void* context, const char* name)
{
	return vkGetDeviceProcAddr((VkDevice)context, name);
}

#ifdef __cplusplus
}
#endif

/* Folded from parallel-psx/volk/volk.c. */

#ifdef __cplusplus
extern "C" {
#endif

/* VOLK_GENERATE_PROTOTYPES_C */
#if defined(VK_VERSION_1_0)
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkCmdBeginQuery vkCmdBeginQuery;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdBlitImage vkCmdBlitImage;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
PFN_vkCmdDispatch vkCmdDispatch;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
PFN_vkCmdEndQuery vkCmdEndQuery;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdExecuteCommands vkCmdExecuteCommands;
PFN_vkCmdFillBuffer vkCmdFillBuffer;
PFN_vkCmdNextSubpass vkCmdNextSubpass;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdPushConstants vkCmdPushConstants;
PFN_vkCmdResetEvent vkCmdResetEvent;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
PFN_vkCmdResolveImage vkCmdResolveImage;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
PFN_vkCmdSetEvent vkCmdSetEvent;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
PFN_vkCmdWaitEvents vkCmdWaitEvents;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkCreateBufferView vkCreateBufferView;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkCreateComputePipelines vkCreateComputePipelines;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
PFN_vkCreateDevice vkCreateDevice;
PFN_vkCreateEvent vkCreateEvent;
PFN_vkCreateFence vkCreateFence;
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkCreateImage vkCreateImage;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkCreateInstance vkCreateInstance;
PFN_vkCreatePipelineCache vkCreatePipelineCache;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkCreateQueryPool vkCreateQueryPool;
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkCreateSampler vkCreateSampler;
PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkDestroyBufferView vkDestroyBufferView;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkDestroyEvent vkDestroyEvent;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkDestroyPipeline vkDestroyPipeline;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkDestroyQueryPool vkDestroyQueryPool;
PFN_vkDestroyRenderPass vkDestroyRenderPass;
PFN_vkDestroySampler vkDestroySampler;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkDestroyShaderModule vkDestroyShaderModule;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkGetEventStatus vkGetEventStatus;
PFN_vkGetFenceStatus vkGetFenceStatus;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
PFN_vkMapMemory vkMapMemory;
PFN_vkMergePipelineCaches vkMergePipelineCaches;
PFN_vkQueueBindSparse vkQueueBindSparse;
PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueueWaitIdle vkQueueWaitIdle;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkResetDescriptorPool vkResetDescriptorPool;
PFN_vkResetEvent vkResetEvent;
PFN_vkResetFences vkResetFences;
PFN_vkSetEvent vkSetEvent;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
PFN_vkWaitForFences vkWaitForFences;
#endif /* defined(VK_VERSION_1_0) */
#if defined(VK_VERSION_1_1)
PFN_vkBindBufferMemory2 vkBindBufferMemory2;
PFN_vkBindImageMemory2 vkBindImageMemory2;
PFN_vkCmdDispatchBase vkCmdDispatchBase;
PFN_vkCmdSetDeviceMask vkCmdSetDeviceMask;
PFN_vkCreateDescriptorUpdateTemplate vkCreateDescriptorUpdateTemplate;
PFN_vkCreateSamplerYcbcrConversion vkCreateSamplerYcbcrConversion;
PFN_vkDestroyDescriptorUpdateTemplate vkDestroyDescriptorUpdateTemplate;
PFN_vkDestroySamplerYcbcrConversion vkDestroySamplerYcbcrConversion;
PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;
PFN_vkEnumeratePhysicalDeviceGroups vkEnumeratePhysicalDeviceGroups;
PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2;
PFN_vkGetDescriptorSetLayoutSupport vkGetDescriptorSetLayoutSupport;
PFN_vkGetDeviceGroupPeerMemoryFeatures vkGetDeviceGroupPeerMemoryFeatures;
PFN_vkGetDeviceQueue2 vkGetDeviceQueue2;
PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2;
PFN_vkGetImageSparseMemoryRequirements2 vkGetImageSparseMemoryRequirements2;
PFN_vkGetPhysicalDeviceExternalBufferProperties vkGetPhysicalDeviceExternalBufferProperties;
PFN_vkGetPhysicalDeviceExternalFenceProperties vkGetPhysicalDeviceExternalFenceProperties;
PFN_vkGetPhysicalDeviceExternalSemaphoreProperties vkGetPhysicalDeviceExternalSemaphoreProperties;
PFN_vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceFeatures2;
PFN_vkGetPhysicalDeviceFormatProperties2 vkGetPhysicalDeviceFormatProperties2;
PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2;
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
PFN_vkGetPhysicalDeviceQueueFamilyProperties2 vkGetPhysicalDeviceQueueFamilyProperties2;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties2 vkGetPhysicalDeviceSparseImageFormatProperties2;
PFN_vkTrimCommandPool vkTrimCommandPool;
PFN_vkUpdateDescriptorSetWithTemplate vkUpdateDescriptorSetWithTemplate;
#endif /* defined(VK_VERSION_1_1) */
#if defined(VK_AMD_buffer_marker)
PFN_vkCmdWriteBufferMarkerAMD vkCmdWriteBufferMarkerAMD;
#endif /* defined(VK_AMD_buffer_marker) */
#if defined(VK_AMD_draw_indirect_count)
PFN_vkCmdDrawIndexedIndirectCountAMD vkCmdDrawIndexedIndirectCountAMD;
PFN_vkCmdDrawIndirectCountAMD vkCmdDrawIndirectCountAMD;
#endif /* defined(VK_AMD_draw_indirect_count) */
#if defined(VK_AMD_shader_info)
PFN_vkGetShaderInfoAMD vkGetShaderInfoAMD;
#endif /* defined(VK_AMD_shader_info) */
#if defined(VK_ANDROID_external_memory_android_hardware_buffer)
PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;
PFN_vkGetMemoryAndroidHardwareBufferANDROID vkGetMemoryAndroidHardwareBufferANDROID;
#endif /* defined(VK_ANDROID_external_memory_android_hardware_buffer) */
#if defined(VK_EXT_acquire_xlib_display)
PFN_vkAcquireXlibDisplayEXT vkAcquireXlibDisplayEXT;
PFN_vkGetRandROutputDisplayEXT vkGetRandROutputDisplayEXT;
#endif /* defined(VK_EXT_acquire_xlib_display) */
#if defined(VK_EXT_calibrated_timestamps)
PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;
PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vkGetPhysicalDeviceCalibrateableTimeDomainsEXT;
#endif /* defined(VK_EXT_calibrated_timestamps) */
#if defined(VK_EXT_conditional_rendering)
PFN_vkCmdBeginConditionalRenderingEXT vkCmdBeginConditionalRenderingEXT;
PFN_vkCmdEndConditionalRenderingEXT vkCmdEndConditionalRenderingEXT;
#endif /* defined(VK_EXT_conditional_rendering) */
#if defined(VK_EXT_debug_marker)
PFN_vkCmdDebugMarkerBeginEXT vkCmdDebugMarkerBeginEXT;
PFN_vkCmdDebugMarkerEndEXT vkCmdDebugMarkerEndEXT;
PFN_vkCmdDebugMarkerInsertEXT vkCmdDebugMarkerInsertEXT;
PFN_vkDebugMarkerSetObjectNameEXT vkDebugMarkerSetObjectNameEXT;
PFN_vkDebugMarkerSetObjectTagEXT vkDebugMarkerSetObjectTagEXT;
#endif /* defined(VK_EXT_debug_marker) */
#if defined(VK_EXT_debug_report)
PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
PFN_vkDebugReportMessageEXT vkDebugReportMessageEXT;
PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;
#endif /* defined(VK_EXT_debug_report) */
#if defined(VK_EXT_debug_utils)
PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;
PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT;
PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT;
PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT;
PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT;
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT;
PFN_vkSubmitDebugUtilsMessageEXT vkSubmitDebugUtilsMessageEXT;
#endif /* defined(VK_EXT_debug_utils) */
#if defined(VK_EXT_direct_mode_display)
PFN_vkReleaseDisplayEXT vkReleaseDisplayEXT;
#endif /* defined(VK_EXT_direct_mode_display) */
#if defined(VK_EXT_discard_rectangles)
PFN_vkCmdSetDiscardRectangleEXT vkCmdSetDiscardRectangleEXT;
#endif /* defined(VK_EXT_discard_rectangles) */
#if defined(VK_EXT_display_control)
PFN_vkDisplayPowerControlEXT vkDisplayPowerControlEXT;
PFN_vkGetSwapchainCounterEXT vkGetSwapchainCounterEXT;
PFN_vkRegisterDeviceEventEXT vkRegisterDeviceEventEXT;
PFN_vkRegisterDisplayEventEXT vkRegisterDisplayEventEXT;
#endif /* defined(VK_EXT_display_control) */
#if defined(VK_EXT_display_surface_counter)
PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT vkGetPhysicalDeviceSurfaceCapabilities2EXT;
#endif /* defined(VK_EXT_display_surface_counter) */
#if defined(VK_EXT_external_memory_host)
PFN_vkGetMemoryHostPointerPropertiesEXT vkGetMemoryHostPointerPropertiesEXT;
#endif /* defined(VK_EXT_external_memory_host) */
#if defined(VK_EXT_hdr_metadata)
PFN_vkSetHdrMetadataEXT vkSetHdrMetadataEXT;
#endif /* defined(VK_EXT_hdr_metadata) */
#if defined(VK_EXT_image_drm_format_modifier)
PFN_vkGetImageDrmFormatModifierPropertiesEXT vkGetImageDrmFormatModifierPropertiesEXT;
#endif /* defined(VK_EXT_image_drm_format_modifier) */
#if defined(VK_EXT_sample_locations)
PFN_vkCmdSetSampleLocationsEXT vkCmdSetSampleLocationsEXT;
PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT vkGetPhysicalDeviceMultisamplePropertiesEXT;
#endif /* defined(VK_EXT_sample_locations) */
#if defined(VK_EXT_transform_feedback)
PFN_vkCmdBeginQueryIndexedEXT vkCmdBeginQueryIndexedEXT;
PFN_vkCmdBeginTransformFeedbackEXT vkCmdBeginTransformFeedbackEXT;
PFN_vkCmdBindTransformFeedbackBuffersEXT vkCmdBindTransformFeedbackBuffersEXT;
PFN_vkCmdDrawIndirectByteCountEXT vkCmdDrawIndirectByteCountEXT;
PFN_vkCmdEndQueryIndexedEXT vkCmdEndQueryIndexedEXT;
PFN_vkCmdEndTransformFeedbackEXT vkCmdEndTransformFeedbackEXT;
#endif /* defined(VK_EXT_transform_feedback) */
#if defined(VK_EXT_validation_cache)
PFN_vkCreateValidationCacheEXT vkCreateValidationCacheEXT;
PFN_vkDestroyValidationCacheEXT vkDestroyValidationCacheEXT;
PFN_vkGetValidationCacheDataEXT vkGetValidationCacheDataEXT;
PFN_vkMergeValidationCachesEXT vkMergeValidationCachesEXT;
#endif /* defined(VK_EXT_validation_cache) */
#if defined(VK_FUCHSIA_imagepipe_surface)
PFN_vkCreateImagePipeSurfaceFUCHSIA vkCreateImagePipeSurfaceFUCHSIA;
#endif /* defined(VK_FUCHSIA_imagepipe_surface) */
#if defined(VK_GOOGLE_display_timing)
PFN_vkGetPastPresentationTimingGOOGLE vkGetPastPresentationTimingGOOGLE;
PFN_vkGetRefreshCycleDurationGOOGLE vkGetRefreshCycleDurationGOOGLE;
#endif /* defined(VK_GOOGLE_display_timing) */
#if defined(VK_KHR_android_surface)
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif /* defined(VK_KHR_android_surface) */
#if defined(VK_KHR_bind_memory2)
PFN_vkBindBufferMemory2KHR vkBindBufferMemory2KHR;
PFN_vkBindImageMemory2KHR vkBindImageMemory2KHR;
#endif /* defined(VK_KHR_bind_memory2) */
#if defined(VK_KHR_create_renderpass2)
PFN_vkCmdBeginRenderPass2KHR vkCmdBeginRenderPass2KHR;
PFN_vkCmdEndRenderPass2KHR vkCmdEndRenderPass2KHR;
PFN_vkCmdNextSubpass2KHR vkCmdNextSubpass2KHR;
PFN_vkCreateRenderPass2KHR vkCreateRenderPass2KHR;
#endif /* defined(VK_KHR_create_renderpass2) */
#if defined(VK_KHR_descriptor_update_template)
PFN_vkCreateDescriptorUpdateTemplateKHR vkCreateDescriptorUpdateTemplateKHR;
PFN_vkDestroyDescriptorUpdateTemplateKHR vkDestroyDescriptorUpdateTemplateKHR;
PFN_vkUpdateDescriptorSetWithTemplateKHR vkUpdateDescriptorSetWithTemplateKHR;
#endif /* defined(VK_KHR_descriptor_update_template) */
#if defined(VK_KHR_device_group)
PFN_vkCmdDispatchBaseKHR vkCmdDispatchBaseKHR;
PFN_vkCmdSetDeviceMaskKHR vkCmdSetDeviceMaskKHR;
PFN_vkGetDeviceGroupPeerMemoryFeaturesKHR vkGetDeviceGroupPeerMemoryFeaturesKHR;
#endif /* defined(VK_KHR_device_group) */
#if defined(VK_KHR_device_group_creation)
PFN_vkEnumeratePhysicalDeviceGroupsKHR vkEnumeratePhysicalDeviceGroupsKHR;
#endif /* defined(VK_KHR_device_group_creation) */
#if defined(VK_KHR_display)
PFN_vkCreateDisplayModeKHR vkCreateDisplayModeKHR;
PFN_vkCreateDisplayPlaneSurfaceKHR vkCreateDisplayPlaneSurfaceKHR;
PFN_vkGetDisplayModePropertiesKHR vkGetDisplayModePropertiesKHR;
PFN_vkGetDisplayPlaneCapabilitiesKHR vkGetDisplayPlaneCapabilitiesKHR;
PFN_vkGetDisplayPlaneSupportedDisplaysKHR vkGetDisplayPlaneSupportedDisplaysKHR;
PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR vkGetPhysicalDeviceDisplayPlanePropertiesKHR;
PFN_vkGetPhysicalDeviceDisplayPropertiesKHR vkGetPhysicalDeviceDisplayPropertiesKHR;
#endif /* defined(VK_KHR_display) */
#if defined(VK_KHR_display_swapchain)
PFN_vkCreateSharedSwapchainsKHR vkCreateSharedSwapchainsKHR;
#endif /* defined(VK_KHR_display_swapchain) */
#if defined(VK_KHR_draw_indirect_count)
PFN_vkCmdDrawIndexedIndirectCountKHR vkCmdDrawIndexedIndirectCountKHR;
PFN_vkCmdDrawIndirectCountKHR vkCmdDrawIndirectCountKHR;
#endif /* defined(VK_KHR_draw_indirect_count) */
#if defined(VK_KHR_external_fence_capabilities)
PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR vkGetPhysicalDeviceExternalFencePropertiesKHR;
#endif /* defined(VK_KHR_external_fence_capabilities) */
#if defined(VK_KHR_external_fence_fd)
PFN_vkGetFenceFdKHR vkGetFenceFdKHR;
PFN_vkImportFenceFdKHR vkImportFenceFdKHR;
#endif /* defined(VK_KHR_external_fence_fd) */
#if defined(VK_KHR_external_fence_win32)
PFN_vkGetFenceWin32HandleKHR vkGetFenceWin32HandleKHR;
PFN_vkImportFenceWin32HandleKHR vkImportFenceWin32HandleKHR;
#endif /* defined(VK_KHR_external_fence_win32) */
#if defined(VK_KHR_external_memory_capabilities)
PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR vkGetPhysicalDeviceExternalBufferPropertiesKHR;
#endif /* defined(VK_KHR_external_memory_capabilities) */
#if defined(VK_KHR_external_memory_fd)
PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR;
#endif /* defined(VK_KHR_external_memory_fd) */
#if defined(VK_KHR_external_memory_win32)
PFN_vkGetMemoryWin32HandleKHR vkGetMemoryWin32HandleKHR;
PFN_vkGetMemoryWin32HandlePropertiesKHR vkGetMemoryWin32HandlePropertiesKHR;
#endif /* defined(VK_KHR_external_memory_win32) */
#if defined(VK_KHR_external_semaphore_capabilities)
PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR vkGetPhysicalDeviceExternalSemaphorePropertiesKHR;
#endif /* defined(VK_KHR_external_semaphore_capabilities) */
#if defined(VK_KHR_external_semaphore_fd)
PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
#endif /* defined(VK_KHR_external_semaphore_fd) */
#if defined(VK_KHR_external_semaphore_win32)
PFN_vkGetSemaphoreWin32HandleKHR vkGetSemaphoreWin32HandleKHR;
PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;
#endif /* defined(VK_KHR_external_semaphore_win32) */
#if defined(VK_KHR_get_display_properties2)
PFN_vkGetDisplayModeProperties2KHR vkGetDisplayModeProperties2KHR;
PFN_vkGetDisplayPlaneCapabilities2KHR vkGetDisplayPlaneCapabilities2KHR;
PFN_vkGetPhysicalDeviceDisplayPlaneProperties2KHR vkGetPhysicalDeviceDisplayPlaneProperties2KHR;
PFN_vkGetPhysicalDeviceDisplayProperties2KHR vkGetPhysicalDeviceDisplayProperties2KHR;
#endif /* defined(VK_KHR_get_display_properties2) */
#if defined(VK_KHR_get_memory_requirements2)
PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR;
PFN_vkGetImageMemoryRequirements2KHR vkGetImageMemoryRequirements2KHR;
PFN_vkGetImageSparseMemoryRequirements2KHR vkGetImageSparseMemoryRequirements2KHR;
#endif /* defined(VK_KHR_get_memory_requirements2) */
#if defined(VK_KHR_get_physical_device_properties2)
PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR;
PFN_vkGetPhysicalDeviceFormatProperties2KHR vkGetPhysicalDeviceFormatProperties2KHR;
PFN_vkGetPhysicalDeviceImageFormatProperties2KHR vkGetPhysicalDeviceImageFormatProperties2KHR;
PFN_vkGetPhysicalDeviceMemoryProperties2KHR vkGetPhysicalDeviceMemoryProperties2KHR;
PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR vkGetPhysicalDeviceQueueFamilyProperties2KHR;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR vkGetPhysicalDeviceSparseImageFormatProperties2KHR;
#endif /* defined(VK_KHR_get_physical_device_properties2) */
#if defined(VK_KHR_get_surface_capabilities2)
PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR vkGetPhysicalDeviceSurfaceCapabilities2KHR;
PFN_vkGetPhysicalDeviceSurfaceFormats2KHR vkGetPhysicalDeviceSurfaceFormats2KHR;
#endif /* defined(VK_KHR_get_surface_capabilities2) */
#if defined(VK_KHR_maintenance1)
PFN_vkTrimCommandPoolKHR vkTrimCommandPoolKHR;
#endif /* defined(VK_KHR_maintenance1) */
#if defined(VK_KHR_maintenance3)
PFN_vkGetDescriptorSetLayoutSupportKHR vkGetDescriptorSetLayoutSupportKHR;
#endif /* defined(VK_KHR_maintenance3) */
#if defined(VK_KHR_push_descriptor)
PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;
#endif /* defined(VK_KHR_push_descriptor) */
#if defined(VK_KHR_sampler_ycbcr_conversion)
PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR;
PFN_vkDestroySamplerYcbcrConversionKHR vkDestroySamplerYcbcrConversionKHR;
#endif /* defined(VK_KHR_sampler_ycbcr_conversion) */
#if defined(VK_KHR_shared_presentable_image)
PFN_vkGetSwapchainStatusKHR vkGetSwapchainStatusKHR;
#endif /* defined(VK_KHR_shared_presentable_image) */
#if defined(VK_KHR_surface)
PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
#endif /* defined(VK_KHR_surface) */
#if defined(VK_KHR_swapchain)
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkQueuePresentKHR vkQueuePresentKHR;
#endif /* defined(VK_KHR_swapchain) */
#if defined(VK_KHR_wayland_surface)
PFN_vkCreateWaylandSurfaceKHR vkCreateWaylandSurfaceKHR;
PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR vkGetPhysicalDeviceWaylandPresentationSupportKHR;
#endif /* defined(VK_KHR_wayland_surface) */
#if defined(VK_KHR_win32_surface)
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR vkGetPhysicalDeviceWin32PresentationSupportKHR;
#endif /* defined(VK_KHR_win32_surface) */
#if defined(VK_KHR_xcb_surface)
PFN_vkCreateXcbSurfaceKHR vkCreateXcbSurfaceKHR;
PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR vkGetPhysicalDeviceXcbPresentationSupportKHR;
#endif /* defined(VK_KHR_xcb_surface) */
#if defined(VK_KHR_xlib_surface)
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR vkGetPhysicalDeviceXlibPresentationSupportKHR;
#endif /* defined(VK_KHR_xlib_surface) */
#if defined(VK_MVK_ios_surface)
PFN_vkCreateIOSSurfaceMVK vkCreateIOSSurfaceMVK;
#endif /* defined(VK_MVK_ios_surface) */
#if defined(VK_MVK_macos_surface)
PFN_vkCreateMacOSSurfaceMVK vkCreateMacOSSurfaceMVK;
#endif /* defined(VK_MVK_macos_surface) */
#if defined(VK_NN_vi_surface)
PFN_vkCreateViSurfaceNN vkCreateViSurfaceNN;
#endif /* defined(VK_NN_vi_surface) */
#if defined(VK_NVX_device_generated_commands)
PFN_vkCmdProcessCommandsNVX vkCmdProcessCommandsNVX;
PFN_vkCmdReserveSpaceForCommandsNVX vkCmdReserveSpaceForCommandsNVX;
PFN_vkCreateIndirectCommandsLayoutNVX vkCreateIndirectCommandsLayoutNVX;
PFN_vkCreateObjectTableNVX vkCreateObjectTableNVX;
PFN_vkDestroyIndirectCommandsLayoutNVX vkDestroyIndirectCommandsLayoutNVX;
PFN_vkDestroyObjectTableNVX vkDestroyObjectTableNVX;
PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX;
PFN_vkRegisterObjectsNVX vkRegisterObjectsNVX;
PFN_vkUnregisterObjectsNVX vkUnregisterObjectsNVX;
#endif /* defined(VK_NVX_device_generated_commands) */
#if defined(VK_NVX_raytracing)
PFN_vkBindAccelerationStructureMemoryNVX vkBindAccelerationStructureMemoryNVX;
PFN_vkCmdBuildAccelerationStructureNVX vkCmdBuildAccelerationStructureNVX;
PFN_vkCmdCopyAccelerationStructureNVX vkCmdCopyAccelerationStructureNVX;
PFN_vkCmdTraceRaysNVX vkCmdTraceRaysNVX;
PFN_vkCmdWriteAccelerationStructurePropertiesNVX vkCmdWriteAccelerationStructurePropertiesNVX;
PFN_vkCompileDeferredNVX vkCompileDeferredNVX;
PFN_vkCreateAccelerationStructureNVX vkCreateAccelerationStructureNVX;
PFN_vkCreateRaytracingPipelinesNVX vkCreateRaytracingPipelinesNVX;
PFN_vkDestroyAccelerationStructureNVX vkDestroyAccelerationStructureNVX;
PFN_vkGetAccelerationStructureHandleNVX vkGetAccelerationStructureHandleNVX;
PFN_vkGetAccelerationStructureMemoryRequirementsNVX vkGetAccelerationStructureMemoryRequirementsNVX;
PFN_vkGetAccelerationStructureScratchMemoryRequirementsNVX vkGetAccelerationStructureScratchMemoryRequirementsNVX;
PFN_vkGetRaytracingShaderHandlesNVX vkGetRaytracingShaderHandlesNVX;
#endif /* defined(VK_NVX_raytracing) */
#if defined(VK_NV_clip_space_w_scaling)
PFN_vkCmdSetViewportWScalingNV vkCmdSetViewportWScalingNV;
#endif /* defined(VK_NV_clip_space_w_scaling) */
#if defined(VK_NV_device_diagnostic_checkpoints)
PFN_vkCmdSetCheckpointNV vkCmdSetCheckpointNV;
PFN_vkGetQueueCheckpointDataNV vkGetQueueCheckpointDataNV;
#endif /* defined(VK_NV_device_diagnostic_checkpoints) */
#if defined(VK_NV_external_memory_capabilities)
PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV vkGetPhysicalDeviceExternalImageFormatPropertiesNV;
#endif /* defined(VK_NV_external_memory_capabilities) */
#if defined(VK_NV_external_memory_win32)
PFN_vkGetMemoryWin32HandleNV vkGetMemoryWin32HandleNV;
#endif /* defined(VK_NV_external_memory_win32) */
#if defined(VK_NV_mesh_shader)
PFN_vkCmdDrawMeshTasksIndirectCountNV vkCmdDrawMeshTasksIndirectCountNV;
PFN_vkCmdDrawMeshTasksIndirectNV vkCmdDrawMeshTasksIndirectNV;
PFN_vkCmdDrawMeshTasksNV vkCmdDrawMeshTasksNV;
#endif /* defined(VK_NV_mesh_shader) */
#if defined(VK_NV_ray_tracing)
PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV;
PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV;
PFN_vkCmdCopyAccelerationStructureNV vkCmdCopyAccelerationStructureNV;
PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV;
PFN_vkCmdWriteAccelerationStructuresPropertiesNV vkCmdWriteAccelerationStructuresPropertiesNV;
PFN_vkCompileDeferredNV vkCompileDeferredNV;
PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV;
PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV;
PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV;
PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV;
PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV;
PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV;
#endif /* defined(VK_NV_ray_tracing) */
#if defined(VK_NV_scissor_exclusive)
PFN_vkCmdSetExclusiveScissorNV vkCmdSetExclusiveScissorNV;
#endif /* defined(VK_NV_scissor_exclusive) */
#if defined(VK_NV_shading_rate_image)
PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV;
PFN_vkCmdSetCoarseSampleOrderNV vkCmdSetCoarseSampleOrderNV;
PFN_vkCmdSetViewportShadingRatePaletteNV vkCmdSetViewportShadingRatePaletteNV;
#endif /* defined(VK_NV_shading_rate_image) */
#if (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1))
PFN_vkCmdPushDescriptorSetWithTemplateKHR vkCmdPushDescriptorSetWithTemplateKHR;
#endif /* (defined(VK_KHR_descriptor_update_template) && defined(VK_KHR_push_descriptor)) || (defined(VK_KHR_push_descriptor) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
PFN_vkGetDeviceGroupPresentCapabilitiesKHR vkGetDeviceGroupPresentCapabilitiesKHR;
PFN_vkGetDeviceGroupSurfacePresentModesKHR vkGetDeviceGroupSurfacePresentModesKHR;
PFN_vkGetPhysicalDevicePresentRectanglesKHR vkGetPhysicalDevicePresentRectanglesKHR;
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_surface)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
#if (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1))
PFN_vkAcquireNextImage2KHR vkAcquireNextImage2KHR;
#endif /* (defined(VK_KHR_device_group) && defined(VK_KHR_swapchain)) || (defined(VK_KHR_swapchain) && defined(VK_VERSION_1_1)) */
/* VOLK_GENERATE_PROTOTYPES_C */

#ifdef __cplusplus
}
#endif
/* === end folded volk === */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef __cplusplus
#include <new> /* placement new; removed when this TU finishes converting to C */
#endif

/* ------------------------------------------------------------------------- *
 * POD_VEC - a typed dynamic array of trivially-relocatable elements, MSVC C89.
 *
 * Replaces std::vector<T> for the renderer's per-frame draw queues, whose
 * elements (BufferVertex, PrimitiveInfo, BlitInfo, VkRect2D, ...) are all POD /
 * trivially relocatable. Growth is a realloc (bitwise relocation - no
 * per-element move-ctor and no exception scaffolding), which is what the hot
 * per-vertex push path wants; the std::vector equivalent emitted an
 * out-of-line _M_realloc_insert plus EH tables on every push. clear keeps the
 * allocation for reuse (these are refilled every frame). The struct is brace-
 * initialisable to { NULL, 0, 0 } so it needs no constructor. */
#define POD_VEC_DECLARE(NAME, T)                                              \
struct NAME {                                                                 \
    T  *items;                                                                \
    int count;                                                                \
    int cap;                                                                  \
};                                                                            \
static inline T   *NAME##_data(struct NAME *v)        { return v->items; }    \
static inline int  NAME##_size(const struct NAME *v)  { return v->count; }    \
static inline int  NAME##_empty(const struct NAME *v) { return v->count == 0; }\
static inline void NAME##_clear(struct NAME *v)       { v->count = 0; }        \
static inline T   *NAME##_at(struct NAME *v, int i)   { return &v->items[i]; } \
static inline T   *NAME##_back(struct NAME *v)        { return &v->items[v->count - 1]; } \
static inline T   *NAME##_front(struct NAME *v)       { return &v->items[0]; } \
static inline int  NAME##_grow_by_one(struct NAME *v) {                        \
    if (v->count == v->cap) {                                                 \
        int ncap = v->cap ? v->cap * 2 : 64;                                  \
        T *nitems = (T *)realloc(v->items, (size_t)ncap * sizeof(T));         \
        if (!nitems)                                                          \
            return 0;                                                         \
        v->items = nitems;                                                    \
        v->cap = ncap;                                                        \
    }                                                                         \
    return 1;                                                                 \
}                                                                             \
static inline void NAME##_push(struct NAME *v, const T *valp) {                \
    T tmp = *valp;  /* copy before grow: *valp may alias items[] (realloc) */ \
    if (NAME##_grow_by_one(v))                                                \
        v->items[v->count++] = tmp;                                           \
}                                                                             \
static inline void NAME##_pop_back(struct NAME *v) { if (v->count) v->count--; } \
static inline void NAME##_free_storage(struct NAME *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; } \
struct NAME##_force_semicolon_

#include <rthreads/rthreads.h>
#include <streams/file_stream.h>

/* Local single-evaluation min/max, so the file does not depend on std::min /
 * std::max from <algorithm>. */
/* min_/max_ as macros (C has no function templates). Arguments must be free of
 * side effects: every call site passes pure expressions, and the two that used
 * accessor calls (get_width()/get_height()) hoist them into locals first.
 * Semantics match the previous templated form (and std::min/std::max),
 * including returning the first argument on a tie; the one former
 * max_<VkDeviceSize>(...) call casts its 32-bit literal to VkDeviceSize so the
 * comparison and result stay 64-bit. */
#define min_(a, b) ((b) < (a) ? (b) : (a))
#define max_(a, b) ((a) < (b) ? (b) : (a))


/* The remainder of this file is the consolidated parallel-psx
 * implementation - originally spread across parallel-psx/util/,
 * parallel-psx/vulkan/, parallel-psx/atlas/,
 * parallel-psx/custom-textures/, and parallel-psx/renderer/ - all
 * folded into this single translation unit alongside the libretro
 * RHI Vulkan backend that was always its only consumer. */

retro_log_printf_t libretro_log;

/* ============================================================
 * util.hpp
 * ============================================================ */

#define LOGE(...) do { if (libretro_log) libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); } while(0)
#define LOGI(...) do { if (libretro_log) libretro_log(RETRO_LOG_INFO, __VA_ARGS__); } while(0)

#ifdef _MSC_VER
#endif

#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~(x))
#elif defined(_MSC_VER)
static inline uint32_t util_clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	return 32;
}

static inline uint32_t util_ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	return 32;
}

#define leading_zeroes(x) util_clz(x)
#define trailing_zeroes(x) util_ctz(x)
#define trailing_ones(x) util_ctz(~(x))
#else
#error "Implement me."
#endif

/* Iterate over each set bit in a uint32_t mask. Inside the body, BIT_VAR holds
 * the bit index. C-style: just expand into a for loop, no captures or lambdas. */
#define FOR_EACH_BIT(value, bit_var)                                          \
	for (uint32_t _fe_v = (uint32_t)(value), bit_var = trailing_zeroes(_fe_v); \
	     _fe_v;                                                                \
	     _fe_v &= _fe_v - 1u, bit_var = trailing_zeroes(_fe_v))

/* Iterate over each contiguous run of 1-bits in a uint32_t mask. BASE_VAR is
 * the bit index of the run's first 1, RANGE_VAR is the run length. */
#define FOR_EACH_BIT_RANGE(value, base_var, range_var)                            \
	for (uint32_t _fe_v = (uint32_t)(value),                                       \
	              base_var = trailing_zeroes(_fe_v),                               \
	              range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u);     \
	     _fe_v;                                                                    \
	     _fe_v &= ~((1u << (base_var + range_var)) - 1u),                          \
	     base_var = trailing_zeroes(_fe_v),                                        \
	     range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u))

	typedef uint64_t Hash;

	/* FNV-1a hasher. Converted from a C++ class to a C89 struct + RHI_INLINE
	 * free functions. Codegen verified byte-identical to the prior inline-method
	 * form under GCC -O2/-O3 (see harness); RHI_INLINE must resolve to
	 * always_inline/__forceinline to preserve that. A bare Hasher must be seeded
	 * with hasher_init() before use (no in-struct default initializer in C89). */
	struct Hasher
	{
		Hash h;
	};

	RHI_INLINE void hasher_init(struct Hasher *s)
	{
		s->h = 0xcbf29ce484222325ull;
	}

	RHI_INLINE void hasher_u32(struct Hasher *s, uint32_t value)
	{
		s->h = (s->h * 0x100000001b3ull) ^ value;
	}

	RHI_INLINE void hasher_u64(struct Hasher *s, uint64_t value)
	{
		hasher_u32(s, (uint32_t)(value & 0xffffffffu));
		hasher_u32(s, (uint32_t)(value >> 32));
	}

	RHI_INLINE void hasher_data(struct Hasher *s, const uint32_t *p, size_t bytes)
	{
		size_t count = bytes / sizeof(uint32_t);
		size_t i;
		for (i = 0; i < count; i++)
			s->h = (s->h * 0x100000001b3ull) ^ p[i];
	}

	RHI_INLINE Hash hasher_get(const struct Hasher *s)
	{
		return s->h;
	}

	/* Refcount carried as a plain struct + free functions instead of a C++
	 * class with methods. count starts at 1 (one owning reference at
	 * construction). Used as the 'reference_count' member of all eight pointee
	 * types; counter_add_ref / counter_release replace the former add_ref /
	 * release methods. */
	struct SingleThreadCounter
	{
		size_t count;
	};
	static inline void counter_init(struct SingleThreadCounter *c) { c->count = 1; }
	static inline void counter_add_ref(struct SingleThreadCounter *c) { c->count++; }
	static inline bool counter_release(struct SingleThreadCounter *c) { return --c->count == 0; }
	typedef struct SingleThreadCounter SingleThreadCounter;

	/* IntrusivePtr<T> has been removed: the eight pointee handle types are now
	 * concrete structs generated by INTRUSIVE_HANDLE_DECLARE below. */

	/* Concrete per-type intrusive handle, replacing IntrusivePtr<T> for the
	 * Vulkan pointee types. Each pointee carries its own HandleCounter and
	 * provides release_reference/add_reference, so the handle is a plain struct
	 * wrapping a raw pointer with the same copy=incref / move=steal / reset=decref
	 * semantics IntrusivePtr had. The pointees are unrelated types (no pointee
	 * derives from another), so only same-type operations are needed - none of
	 * IntrusivePtr's cross-type template conversions were ever instantiated. T
	 * must be complete at the point of declaration. */
#define INTRUSIVE_HANDLE_DECLARE(HandleName, T)                                       \
	struct HandleName {                                                              \
		T *data;                                                                     \
		HandleName() : data(NULL) {}                                                 \
		explicit HandleName(T *handle) : data(handle) {}                             \
		T &operator*() { return *data; }                                            \
		const T &operator*() const { return *data; }                                \
		T *operator->() { return data; }                                            \
		const T *operator->() const { return data; }                                \
		explicit operator bool() const { return data != NULL; }                     \
		bool operator==(const HandleName &o) const { return data == o.data; }        \
		bool operator!=(const HandleName &o) const { return data != o.data; }        \
		T *get() { return data; }                                                   \
		const T *get() const { return data; }                                       \
		void reset() { if (data) data->release_reference(); data = NULL; }            \
		HandleName(const HandleName &o) : data(o.data) { if (data) data->add_reference(); } \
		HandleName &operator=(const HandleName &o) {                                 \
			if (this != &o) { reset(); data = o.data; if (data) data->add_reference(); } \
			return *this;                                                            \
		}                                                                            \
		HandleName(HandleName &&o) : data(o.data) { o.data = NULL; }                  \
		HandleName &operator=(HandleName &&o) {                                      \
			if (this != &o) { reset(); data = o.data; o.data = NULL; }                \
			return *this;                                                            \
		}                                                                            \
		~HandleName() { reset(); }                                                   \
	}

	/* Concrete intrusive doubly-linked list. Links are type-erased - prev/next are
	 * plain IntrusiveListNode pointers - and a node's owning type is recovered by
	 * casting, valid because the node base is the first member (offset 0). A single
	 * concrete node base (IntrusiveListNode) and list (IntrusiveListC) serve every
	 * intrusive list in the file. */
	struct IntrusiveListNode
	{
		struct IntrusiveListNode *prev;
		struct IntrusiveListNode *next;
	};

	struct IntrusiveListC
	{
		struct IntrusiveListNode *head = NULL;
	};

	static inline void ilist_clear(struct IntrusiveListC *list)
	{
		list->head = NULL;
	}

	static inline bool ilist_empty(const struct IntrusiveListC *list)
	{
		return list->head == NULL;
	}

	static inline struct IntrusiveListNode *ilist_begin(const struct IntrusiveListC *list)
	{
		return list->head;
	}

	/* Unlink node and return the node that followed it. */
	static inline struct IntrusiveListNode *ilist_erase(struct IntrusiveListC *list,
			struct IntrusiveListNode *node)
	{
		struct IntrusiveListNode *next = node->next;
		struct IntrusiveListNode *prev = node->prev;
		if (prev)
			prev->next = next;
		else
			list->head = next;
		if (next)
			next->prev = prev;
		return next;
	}

	static inline void ilist_insert_front(struct IntrusiveListC *list,
			struct IntrusiveListNode *node)
	{
		if (list->head)
			list->head->prev = node;
		node->next = list->head;
		node->prev = NULL;
		list->head = node;
	}

	static inline void ilist_move_to_front(struct IntrusiveListC *list,
			struct IntrusiveListC *other, struct IntrusiveListNode *node)
	{
		ilist_erase(other, node);
		ilist_insert_front(list, node);
	}

	/* Concrete, element-size-driven object pool (the de-templated form of the
	 * standalone ObjectPool<T> instantiations). Same slab + vacant-stack design as
	 * ObjectPool<T>, but type-erased: allocate_raw() returns an uninitialised slot
	 * of element_size bytes and free_raw() returns one to the free list, so a single
	 * concrete type serves every pooled object. Construction/destruction is done by
	 * the caller via placement new / explicit destructor (matching the
	 * malloc + placement-new idiom used for Device/Renderer). */
	struct ObjectPoolRaw
	{
		void **vacants;
		int    vac_count;
		int    vac_cap;
		void **memory;
		int    mem_count;
		int    mem_cap;
		size_t element_size;
	};

	static inline void object_pool_raw_init(struct ObjectPoolRaw *p, size_t element_size)
	{
		p->vacants      = NULL;
		p->vac_count    = 0;
		p->vac_cap      = 0;
		p->memory       = NULL;
		p->mem_count    = 0;
		p->mem_cap      = 0;
		p->element_size = element_size;
	}

	static inline void object_pool_raw_vac_push(struct ObjectPoolRaw *p, void *v)
	{
		if (p->vac_count == p->vac_cap)
		{
			int ncap   = p->vac_cap ? p->vac_cap * 2 : 64;
			void **nv = (void **)realloc(p->vacants, (size_t)ncap * sizeof(void *));
			if (!nv)
				return;
			p->vacants = nv;
			p->vac_cap = ncap;
		}
		p->vacants[p->vac_count++] = v;
	}

	static inline void object_pool_raw_mem_push(struct ObjectPoolRaw *p, void *v)
	{
		if (p->mem_count == p->mem_cap)
		{
			int ncap   = p->mem_cap ? p->mem_cap * 2 : 8;
			void **nm = (void **)realloc(p->memory, (size_t)ncap * sizeof(void *));
			if (!nm)
				return;
			p->memory  = nm;
			p->mem_cap = ncap;
		}
		p->memory[p->mem_count++] = v;
	}

	/* Return an uninitialised slot of element_size bytes (or NULL on OOM). The
	 * caller placement-constructs into it. */
	static inline void *object_pool_raw_allocate(struct ObjectPoolRaw *p)
	{
		if (p->vac_count == 0)
		{
			unsigned num_objects = 64u << (unsigned)p->mem_count;
			char    *ptr         = (char *)malloc(num_objects * p->element_size);
			unsigned i;
			if (!ptr)
				return NULL;
			for (i = 0; i < num_objects; i++)
				object_pool_raw_vac_push(p, ptr + (size_t)i * p->element_size);
			object_pool_raw_mem_push(p, ptr);
		}
		return p->vacants[--p->vac_count];
	}

	/* Return a slot to the free list. The caller has already destroyed the object. */
	static inline void object_pool_raw_free(struct ObjectPoolRaw *p, void *ptr)
	{
		object_pool_raw_vac_push(p, ptr);
	}

	static inline void object_pool_raw_clear(struct ObjectPoolRaw *p)
	{
		int i;
		for (i = 0; i < p->mem_count; i++)
			::free(p->memory[i]);
		p->vac_count = 0;
		p->mem_count = 0;
	}

	static inline void object_pool_raw_deinit(struct ObjectPoolRaw *p)
	{
		object_pool_raw_clear(p);
		::free(p->vacants);
		::free(p->memory);
		p->vacants = NULL;
		p->memory  = NULL;
		p->vac_cap = 0;
		p->mem_cap = 0;
	}

	/* Concrete intrusive-hash-map node base (the de-templated form of
	 * IntrusiveHashMapEnabled<T>). It carries the intrusive list links (for the
	 * holder's value list) and the hash key. Every hash-map node type embeds this as
	 * its first member (offset 0, static_assert'd), so the holder recovers the node
	 * from an IntrusiveHashMapNode* by casting - exactly as the template did via
	 * static_cast through the IntrusiveListEnabled/IntrusiveHashMapEnabled bases. */
	struct IntrusiveHashMapNode
	{
		struct IntrusiveListNode list_node; /* must stay first (offset 0) */
		Hash key;
	};

	/* IntrusivePODWrapper stays a template (it wraps an arbitrary POD value for the
	 * pipeline cache and the TemporaryHashmap recycle map), but it now embeds the
	 * concrete node base as its first member instead of inheriting the old CRTP
	 * IntrusiveHashMapEnabled base, so the concrete holder can treat it like every
	 * other node. get() returns the payload, matching the previous interface. */
	/* Concrete POD-wrapper node types (the de-templated IntrusivePODWrapper<T>). Two
	 * payloads are wrapped: a VkPipeline (the per-program pipeline cache) and a void*
	 * iterator (the TemporaryHashmap recycle map). Each embeds the hash-map node base
	 * first (offset 0) and exposes get() returning the payload, matching the previous
	 * template interface so call sites are unchanged. */
	struct IntrusivePODWrapperPipeline
	{
		struct IntrusiveHashMapNode node; /* must stay first (offset 0) */
		VkPipeline value;
	};

	struct IntrusivePODWrapperPtr
	{
		struct IntrusiveHashMapNode node; /* must stay first (offset 0) */
		void *value;
	};

	/* The concrete hash map recovers each wrapper from an IntrusiveHashMapNode* by
	 * casting, valid only if the node base is at offset 0. */
	static_assert(offsetof(struct IntrusivePODWrapperPipeline, node) == 0,
			"IntrusivePODWrapperPipeline.node must be first");
	static_assert(offsetof(struct IntrusivePODWrapperPtr, node) == 0,
			"IntrusivePODWrapperPtr.node must be first");

	/* Concrete, type-erased open-addressing hash table (the de-templated form of
	 * IntrusiveHashMapHolder<T>). It is non-owning: it only arranges a table and a
	 * list of IntrusiveHashMapNode pointers; the nodes are owned by an object pool.
	 * Callers cast their node type to IntrusiveHashMapNode* (offset 0). All the
	 * probe / grow / LRU semantics match the template exactly (validated under
	 * ASan). */
	struct IntrusiveHashMapHolderC
	{
		struct IntrusiveHashMapNode **items;
		size_t count;
		size_t cap;
		struct IntrusiveListC list;
		unsigned load_count;
	};

	enum { HMHOLDER_InitialSize = 16, HMHOLDER_InitialLoadCount = 3 };

	static inline Hash hmholder_get_hash(const struct IntrusiveHashMapNode *value)
	{
		return value->key;
	}

	static inline struct IntrusiveHashMapNode *hmholder_find(
			const struct IntrusiveHashMapHolderC *h, Hash hash)
	{
		Hash hash_mask, masked;
		unsigned i;
		if (h->count == 0)
			return NULL;
		hash_mask = h->count - 1;
		masked    = hash & hash_mask;
		for (i = 0; i < h->load_count; i++)
		{
			if (h->items[masked] && hmholder_get_hash(h->items[masked]) == hash)
				return h->items[masked];
			masked = (masked + 1) & hash_mask;
		}
		return NULL;
	}

	static inline bool hmholder_insert_inner(struct IntrusiveHashMapHolderC *h,
			struct IntrusiveHashMapNode *value)
	{
		Hash hash_mask = h->count - 1;
		Hash hash      = hmholder_get_hash(value);
		Hash masked    = hash & hash_mask;
		unsigned i;
		for (i = 0; i < h->load_count; i++)
		{
			if (!h->items[masked])
			{
				h->items[masked] = value;
				return true;
			}
			masked = (masked + 1) & hash_mask;
		}
		return false;
	}

	static inline void hmholder_resize_null(struct IntrusiveHashMapHolderC *h, size_t n)
	{
		size_t i;
		if (n > h->cap)
		{
			struct IntrusiveHashMapNode **ni = (struct IntrusiveHashMapNode **)realloc(h->items,
					n * sizeof(struct IntrusiveHashMapNode *));
			if (!ni)
				return;
			h->items = ni;
			h->cap   = n;
		}
		for (i = 0; i < n; i++)
			h->items[i] = NULL;
		h->count = n;
	}

	static inline void hmholder_grow(struct IntrusiveHashMapHolderC *h)
	{
		bool success;
		do
		{
			{
				size_t i, n = h->count;
				for (i = 0; i < n; i++)
					h->items[i] = NULL;
			}

			if (h->count == 0)
			{
				hmholder_resize_null(h, HMHOLDER_InitialSize);
				h->load_count = HMHOLDER_InitialLoadCount;
			}
			else
			{
				hmholder_resize_null(h, h->count * 2);
				h->load_count++;
			}

			success = true;
			{
				struct IntrusiveListNode *n;
				for (n = ilist_begin(&h->list); n; n = n->next)
				{
					if (!hmholder_insert_inner(h, (struct IntrusiveHashMapNode *)n))
					{
						success = false;
						break;
					}
				}
			}
		} while (!success);
	}

	static inline struct IntrusiveHashMapNode *hmholder_insert_yield(
			struct IntrusiveHashMapHolderC *h, struct IntrusiveHashMapNode **value)
	{
		Hash hash_mask, hash, masked;
		unsigned i;
		if (h->count == 0)
			hmholder_grow(h);

		hash_mask = h->count - 1;
		hash      = hmholder_get_hash(*value);
		masked    = hash & hash_mask;

		for (i = 0; i < h->load_count; i++)
		{
			if (h->items[masked] && hmholder_get_hash(h->items[masked]) == hash)
			{
				struct IntrusiveHashMapNode *ret = *value;
				*value = h->items[masked];
				return ret;
			}
			else if (!h->items[masked])
			{
				h->items[masked] = *value;
				ilist_insert_front(&h->list, &(*value)->list_node);
				return NULL;
			}
			masked = (masked + 1) & hash_mask;
		}

		hmholder_grow(h);
		return hmholder_insert_yield(h, value);
	}

	static inline struct IntrusiveHashMapNode *hmholder_insert_replace(
			struct IntrusiveHashMapHolderC *h, struct IntrusiveHashMapNode *value)
	{
		Hash hash_mask, hash, masked;
		unsigned i;
		if (h->count == 0)
			hmholder_grow(h);

		hash_mask = h->count - 1;
		hash      = hmholder_get_hash(value);
		masked    = hash & hash_mask;

		for (i = 0; i < h->load_count; i++)
		{
			if (h->items[masked] && hmholder_get_hash(h->items[masked]) == hash)
			{
				struct IntrusiveHashMapNode *tmp = h->items[masked];
				h->items[masked] = value;
				value = tmp;
				ilist_erase(&h->list, &value->list_node);
				ilist_insert_front(&h->list, &h->items[masked]->list_node);
				return value;
			}
			else if (!h->items[masked])
			{
				h->items[masked] = value;
				ilist_insert_front(&h->list, &value->list_node);
				return NULL;
			}
			masked = (masked + 1) & hash_mask;
		}

		hmholder_grow(h);
		return hmholder_insert_replace(h, value);
	}

	static inline struct IntrusiveHashMapNode *hmholder_erase(
			struct IntrusiveHashMapHolderC *h, Hash hash)
	{
		Hash hash_mask = h->count - 1;
		Hash masked    = hash & hash_mask;
		unsigned i;
		for (i = 0; i < h->load_count; i++)
		{
			if (h->items[masked] && hmholder_get_hash(h->items[masked]) == hash)
			{
				struct IntrusiveHashMapNode *value = h->items[masked];
				ilist_erase(&h->list, &value->list_node);
				h->items[masked] = NULL;
				return value;
			}
			masked = (masked + 1) & hash_mask;
		}
		return NULL;
	}

	static inline void hmholder_clear(struct IntrusiveHashMapHolderC *h)
	{
		ilist_clear(&h->list);
		::free(h->items);
		h->items      = NULL;
		h->count      = 0;
		h->cap        = 0;
		h->load_count = 0;
	}

	static inline void hmholder_init_empty(struct IntrusiveHashMapHolderC *h)
	{
		h->items      = NULL;
		h->count      = 0;
		h->cap        = 0;
		ilist_clear(&h->list);
		h->load_count = 0;
	}

	static inline void hmholder_deinit(struct IntrusiveHashMapHolderC *h)
	{
		::free(h->items);
		h->items = NULL;
		h->count = 0;
		h->cap   = 0;
	}

	static inline struct IntrusiveListNode *hmholder_begin(struct IntrusiveHashMapHolderC *h)
	{
		return ilist_begin(&h->list);
	}




	/* Concrete, per-type form of IntrusiveHashMap<T>. Stamps a struct that owns the
	 * (already concrete) open-addressing holder plus an object pool, and the full
	 * public API the template exposed. Everything here is T-independent except the
	 * sizeof(T) handed to the pool and the per-type element destructor DESTROY_FN,
	 * which clear()/erase() call through (the five node types have non-trivial
	 * destructors that release Vk objects; pass a no-op for trivial POD elements).
	 * The variadic emplace and allocate members are NOT stamped here: each element
	 * type has a distinct constructor, so those are written per type next to each
	 * map. The iterator is a bare list-node pointer; the iter_get helper recovers
	 * the T pointer (node base is at offset 0). */
	#define VK_HASHMAP_DECLARE(NAME, T, DESTROY_FN)                                 \
		struct NAME                                                                 \
		{                                                                           \
			struct IntrusiveHashMapHolderC hashmap;                                 \
			struct ObjectPoolRaw pool;                                              \
		};                                                                          \
		static inline void NAME##_init(struct NAME *m)                              \
		{                                                                           \
			hmholder_init_empty(&m->hashmap);                                       \
			object_pool_raw_init(&m->pool, sizeof(T));                              \
		}                                                                           \
		static inline void NAME##_clear(struct NAME *m)                             \
		{                                                                           \
			struct IntrusiveListNode *itr = ilist_begin(&m->hashmap.list);          \
			while (itr)                                                             \
			{                                                                       \
				T *to_free = (T *)itr;                                              \
				itr = itr->next;                                                    \
				DESTROY_FN(to_free);                                                \
				object_pool_raw_free(&m->pool, to_free);                            \
			}                                                                       \
			hmholder_clear(&m->hashmap);                                            \
		}                                                                           \
		static inline void NAME##_deinit(struct NAME *m)                            \
		{                                                                           \
			NAME##_clear(m);                                                        \
			hmholder_deinit(&m->hashmap);                                           \
			object_pool_raw_deinit(&m->pool);                                       \
		}                                                                           \
		static inline T *NAME##_find(const struct NAME *m, Hash hash)               \
		{                                                                           \
			return (T *)hmholder_find(&m->hashmap, hash);                           \
		}                                                                           \
		static inline void NAME##_erase(struct NAME *m, Hash hash)                  \
		{                                                                           \
			T *value = (T *)hmholder_erase(&m->hashmap, hash);                      \
			if (value)                                                              \
			{                                                                       \
				DESTROY_FN(value);                                                  \
				object_pool_raw_free(&m->pool, value);                             \
			}                                                                       \
		}                                                                           \
		static inline T *NAME##_insert_replace(struct NAME *m, Hash hash, T *value) \
		{                                                                           \
			T *to_delete;                                                           \
			((struct IntrusiveHashMapNode *)value)->key = hash;                     \
			to_delete = (T *)hmholder_insert_replace(&m->hashmap,                   \
					(struct IntrusiveHashMapNode *)value);                          \
			if (to_delete)                                                          \
			{                                                                       \
				DESTROY_FN(to_delete);                                              \
				object_pool_raw_free(&m->pool, to_delete);                         \
			}                                                                       \
			return value;                                                           \
		}                                                                           \
		static inline T *NAME##_insert_yield(struct NAME *m, Hash hash, T *value)   \
		{                                                                           \
			struct IntrusiveHashMapNode *node = (struct IntrusiveHashMapNode *)value; \
			T *to_delete;                                                           \
			node->key = hash;                                                       \
			to_delete = (T *)hmholder_insert_yield(&m->hashmap, &node);             \
			if (to_delete)                                                          \
			{                                                                       \
				DESTROY_FN(to_delete);                                              \
				object_pool_raw_free(&m->pool, to_delete);                         \
			}                                                                       \
			return (T *)node;                                                       \
		}                                                                           \
		static inline struct IntrusiveListNode *NAME##_begin(struct NAME *m)        \
		{                                                                           \
			return ilist_begin(&m->hashmap.list);                                   \
		}                                                                           \
		static inline T *NAME##_iter_get(struct IntrusiveListNode *it)              \
		{                                                                           \
			return (T *)it;                                                         \
		}

	/* The two POD-payload maps. Elements are trivially destructible, so the destroy
	 * hook is a no-op. The per-type emplace constructs the wrapper in the pool with
	 * placement new, matching what the template's allocate()+insert did. */
	static inline void vk_ptr_map_destroy(IntrusivePODWrapperPtr *p) { (void)p; }
	static inline void vk_pipeline_map_destroy(IntrusivePODWrapperPipeline *p) { (void)p; }

	VK_HASHMAP_DECLARE(vk_ptr_map, IntrusivePODWrapperPtr, vk_ptr_map_destroy)
	VK_HASHMAP_DECLARE(vk_pipeline_map, IntrusivePODWrapperPipeline, vk_pipeline_map_destroy)

	static inline IntrusivePODWrapperPtr *vk_ptr_map_emplace_replace(struct vk_ptr_map *m,
			Hash hash, void *value)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		IntrusivePODWrapperPtr *t;
		if (!slot)
			return NULL;
		t = (IntrusivePODWrapperPtr *)slot;
		t->value = value;
		return vk_ptr_map_insert_replace(m, hash, t);
	}

	static inline IntrusivePODWrapperPipeline *vk_pipeline_map_emplace_yield(
			struct vk_pipeline_map *m, Hash hash, VkPipeline value)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		IntrusivePODWrapperPipeline *t;
		if (!slot)
			return NULL;
		t = (IntrusivePODWrapperPipeline *)slot;
		t->value = value;
		return vk_pipeline_map_insert_yield(m, hash, t);
	}

	/* Concrete temporary-hashmap node base (de-templated TemporaryHashmapEnabled<T>
	 * plus the intrusive list links the node carried via IntrusiveListEnabled<T>).
	 * Every TemporaryHashmap node type embeds this; because one of them
	 * (FramebufferNode) also has a non-empty Framebuffer base, this base is NOT
	 * necessarily at offset 0, so the ring lists recover the node with container-of
	 * (TH_NODE_OF) rather than an offset-0 cast. */
	struct TemporaryHashmapNode
	{
		struct IntrusiveListNode list_node;
		Hash hash;
		unsigned index;
	};

	/* Recover the owning node from a ring list-node pointer. NODE_TYPE names the
	 * concrete node, MEMBER is its TemporaryHashmapNode member (always th_node). */
	#define TH_NODE_OF(NODE_TYPE, ln) \
		((NODE_TYPE *)((char *)(ln) - offsetof(NODE_TYPE, th_node) \
			- offsetof(struct TemporaryHashmapNode, list_node)))

	/* Concrete, per-type form of TemporaryHashmap<T,RingSize,ReuseObjects>. Stamps a
	 * struct holding the ring of intrusive lists, the object pool, the recycle index
	 * map (vk_ptr_map) and the vacant free-list, plus every method the template had
	 * except the variadic emplace/make_vacant (each node type has a distinct
	 * constructor, written per type next to each instance). RING and REUSE are the
	 * former template non-type parameters; REUSE is a literal 0/1 so the compiler
	 * folds the dead arm in begin_frame exactly as the template's bool constant did.
	 * DESTROY(T*) runs the node destructor (kept until this TU moves to a C compiler).
	 * Nodes are recovered from a ring list-node with TH_NODE_OF(T, ...). */
	#define VK_TEMPHASH_DECLARE(NAME, T, RING, REUSE, DESTROY)                      \
		struct NAME                                                                 \
		{                                                                           \
			struct IntrusiveListC rings[RING];                                      \
			struct ObjectPoolRaw object_pool;                                       \
			unsigned index;                                                         \
			struct vk_ptr_map hashmap;                                              \
			T **vacant_items;                                                       \
			size_t vacant_count;                                                    \
			size_t vacant_cap;                                                      \
		};                                                                          \
		static inline void NAME##_vacant_push(struct NAME *m, T *v)                 \
		{                                                                           \
			if (m->vacant_count == m->vacant_cap)                                   \
			{                                                                       \
				size_t ncap = m->vacant_cap ? m->vacant_cap * 2 : 16;               \
				T **nv = (T **)realloc(m->vacant_items, ncap * sizeof(T *));         \
				if (!nv)                                                            \
					return;                                                         \
				m->vacant_items = nv;                                               \
				m->vacant_cap   = ncap;                                             \
			}                                                                       \
			m->vacant_items[m->vacant_count++] = v;                                 \
		}                                                                           \
		static inline void NAME##_init_empty(struct NAME *m)                        \
		{                                                                           \
			unsigned i;                                                             \
			for (i = 0; i < (RING); i++)                                            \
				ilist_clear(&m->rings[i]);                                          \
			object_pool_raw_init(&m->object_pool, sizeof(T));                       \
			m->index = 0;                                                           \
			m->vacant_items = NULL;                                                 \
			m->vacant_count = 0;                                                    \
			m->vacant_cap   = 0;                                                    \
			vk_ptr_map_init(&m->hashmap);                                           \
		}                                                                           \
		static inline void NAME##_clear(struct NAME *m)                             \
		{                                                                           \
			unsigned r;                                                             \
			size_t i, n;                                                            \
			for (r = 0; r < (RING); r++)                                            \
			{                                                                       \
				struct IntrusiveListNode *nd = ilist_begin(&m->rings[r]);           \
				while (nd)                                                          \
				{                                                                   \
					T *node = TH_NODE_OF(T, nd);                                     \
					nd = nd->next;                                                  \
					DESTROY(node);                                                  \
					object_pool_raw_free(&m->object_pool, node);                    \
				}                                                                   \
				ilist_clear(&m->rings[r]);                                          \
			}                                                                       \
			vk_ptr_map_clear(&m->hashmap);                                          \
			n = m->vacant_count;                                                    \
			for (i = 0; i < n; i++)                                                 \
			{                                                                       \
				T *node = m->vacant_items[i];                                       \
				DESTROY(node);                                                      \
				object_pool_raw_free(&m->object_pool, node);                        \
			}                                                                       \
			m->vacant_count = 0;                                                    \
			object_pool_raw_clear(&m->object_pool);                                 \
		}                                                                           \
		static inline void NAME##_deinit(struct NAME *m)                            \
		{                                                                           \
			NAME##_clear(m);                                                        \
			object_pool_raw_deinit(&m->object_pool);                                \
			free(m->vacant_items);                                                  \
		}                                                                           \
		static inline void NAME##_begin_frame(struct NAME *m)                       \
		{                                                                           \
			struct IntrusiveListNode *nd;                                           \
			m->index = (m->index + 1) & ((RING) - 1);                               \
			nd = ilist_begin(&m->rings[m->index]);                                  \
			while (nd)                                                              \
			{                                                                       \
				T *node = TH_NODE_OF(T, nd);                                         \
				nd = nd->next;                                                      \
				vk_ptr_map_erase(&m->hashmap, node->th_node.hash);                  \
				if (REUSE)                                                          \
					NAME##_vacant_push(m, node);                                    \
				else                                                                \
				{                                                                   \
					DESTROY(node);                                                  \
					object_pool_raw_free(&m->object_pool, node);                    \
				}                                                                   \
			}                                                                       \
			ilist_clear(&m->rings[m->index]);                                       \
		}                                                                           \
		static inline T *NAME##_request(struct NAME *m, Hash hash)                  \
		{                                                                           \
			IntrusivePODWrapperPtr *v = vk_ptr_map_find(&m->hashmap, hash);         \
			if (v)                                                                  \
			{                                                                       \
				T *node = (T *)v->value;                                            \
				if (node->th_node.index != m->index)                               \
				{                                                                   \
					ilist_move_to_front(&m->rings[m->index],                        \
							&m->rings[node->th_node.index],                         \
							&node->th_node.list_node);                              \
					node->th_node.index = m->index;                                 \
				}                                                                   \
				return node;                                                        \
			}                                                                       \
			return NULL;                                                            \
		}                                                                           \
		static inline T *NAME##_request_vacant(struct NAME *m, Hash hash)           \
		{                                                                           \
			T *top;                                                                 \
			if (m->vacant_count == 0)                                               \
				return NULL;                                                        \
			top = m->vacant_items[--m->vacant_count];                              \
			top->th_node.index = m->index;                                          \
			top->th_node.hash  = hash;                                              \
			vk_ptr_map_emplace_replace(&m->hashmap, hash, (void *)top);             \
			ilist_insert_front(&m->rings[m->index], &top->th_node.list_node);       \
			return top;                                                             \
		}


/* ============================================================
 * vulkan.hpp
 * ============================================================ */

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
/* Workaround silly Xlib headers that define macros for these globally :( */
#undef None
#undef Bool
#endif


#define V_S(x) #x
#define V_S_(x) V_S(x)
#define S__LINE__ V_S_(__LINE__)

#ifdef VULKAN_DEBUG
#define VK_ASSERT(x)                                             \
	do                                                           \
	{                                                            \
		if (!(x))                                                \
		{                                                        \
			LOGE("Vulkan error at %s:%d.\n", __FILE__, __LINE__); \
			abort();                                             \
		}                                                        \
	} while (0)
#else
#define VK_ASSERT(x) ((void)0)
#endif

	struct NoCopyNoMove
	{
		NoCopyNoMove() = default;
		NoCopyNoMove(const NoCopyNoMove &) = delete;
		void operator=(const NoCopyNoMove &) = delete;
	};

	struct DeviceFeatures
	{
		bool supports_external = false;
		bool supports_dedicated = false;
		bool supports_debug_marker = false;
		VkPhysicalDeviceFeatures enabled_features = {};
	};

	enum VendorID
	{
		VENDOR_ID_NVIDIA = 0x10de,
		VENDOR_ID_ARM = 0x13b5
	};

	/* The live Vulkan context (instance/device/queues). Formerly a class with a
	 * multi-argument constructor (load loaders, create the device, mark valid) and
	 * a destructor that called destroy(); now a plain struct driven by
	 * context_init / context_deinit. All getters and the device-creation helpers
	 * stay as struct methods. It is heap-allocated, not pooled or refcounted. */
	struct Context
	{
		VkDevice device;
		VkInstance instance;
		VkPhysicalDevice gpu;

		VkPhysicalDeviceProperties gpu_props;
		VkPhysicalDeviceMemoryProperties mem_props;

		VkQueue graphics_queue;
		VkQueue compute_queue;
		VkQueue transfer_queue;
		uint32_t graphics_queue_family;
		uint32_t compute_queue_family;
		uint32_t transfer_queue_family;

		bool owned_device;
		bool valid;
		DeviceFeatures ext;
	};

	bool context_init_loader(PFN_vkGetInstanceProcAddr addr);
	bool context_create_device(struct Context *self, VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
			unsigned num_required_device_extensions, const char **required_device_layers,
			unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);
	void context_destroy(struct Context *self);

	static inline VkInstance context_get_instance(const struct Context *self) { return self->instance; }
	static inline VkPhysicalDevice context_get_gpu(const struct Context *self) { return self->gpu; }
	static inline VkDevice context_get_device(const struct Context *self) { return self->device; }
	static inline VkQueue context_get_graphics_queue(const struct Context *self) { return self->graphics_queue; }
	static inline VkQueue context_get_compute_queue(const struct Context *self) { return self->compute_queue; }
	static inline VkQueue context_get_transfer_queue(const struct Context *self) { return self->transfer_queue; }
	static inline const VkPhysicalDeviceProperties *context_get_gpu_props(const struct Context *self) { return &self->gpu_props; }
	static inline const VkPhysicalDeviceMemoryProperties *context_get_mem_props(const struct Context *self) { return &self->mem_props; }
	static inline uint32_t context_get_graphics_queue_family(const struct Context *self) { return self->graphics_queue_family; }
	static inline uint32_t context_get_compute_queue_family(const struct Context *self) { return self->compute_queue_family; }
	static inline uint32_t context_get_transfer_queue_family(const struct Context *self) { return self->transfer_queue_family; }
	static inline void context_release_device(struct Context *self) { self->owned_device = false; }
	static inline const DeviceFeatures *context_get_enabled_device_features(const struct Context *self) { return &self->ext; }
	static inline bool context_is_valid(const struct Context *self) { return self->valid; }

	static bool context_init(Context *ctx, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
			const char **required_device_extensions, unsigned num_required_device_extensions,
			const char **required_device_layers, unsigned num_required_device_layers,
			const VkPhysicalDeviceFeatures *required_features);
	static void context_deinit(Context *ctx);

	using HandleCounter = SingleThreadCounter;


	static const unsigned VULKAN_NUM_DESCRIPTOR_SETS = 4;
	static const unsigned VULKAN_NUM_BINDINGS = 16;
	static const unsigned VULKAN_NUM_ATTACHMENTS = 8;
	static const unsigned VULKAN_NUM_VERTEX_ATTRIBS = 16;
	static const unsigned VULKAN_NUM_VERTEX_BUFFERS = 4;
	static const unsigned VULKAN_PUSH_CONSTANT_SIZE = 128;
	static const unsigned VULKAN_NUM_SPEC_CONSTANTS = 8;

	struct ImplementationWorkarounds
	{
		bool optimize_all_graphics_barrier = false;
	};

	/* TextureFormatLayout: computes mip/layer byte layout for a texture upload
	 * staging buffer. Converted from a C++ class to a plain C struct + tfl_* free
	 * functions. The nested MipInfo type is hoisted to file scope as
	 * TextureFormatLayoutMipInfo. The three static helpers (format_block_size,
	 * format_block_dim, num_miplevels) become tfl_* free functions taking no self. */
	struct TextureFormatLayoutMipInfo
	{
		size_t offset;
		uint32_t width;
		uint32_t height;
		uint32_t depth;

		uint32_t block_image_height;
		uint32_t block_row_length;
		uint32_t image_height;
		uint32_t row_length;
	};

	struct TextureFormatLayout
	{
		uint8_t *buffer;
		size_t buffer_size;

		VkImageType image_type;
		VkFormat format;
		size_t required_size;

		uint32_t block_stride;
		uint32_t mip_levels;
		uint32_t array_layers;
		uint32_t block_dim_x;
		uint32_t block_dim_y;

		TextureFormatLayoutMipInfo mips[16];
	};

	/* Establishes the default-constructed state (the former default member
	 * initializers). Callers that declare a TextureFormatLayout must call this. */
	static inline void tfl_init(struct TextureFormatLayout *self)
	{
		memset(self, 0, sizeof(*self));
		self->image_type   = VK_IMAGE_TYPE_RANGE_SIZE;
		self->format       = VK_FORMAT_UNDEFINED;
		self->block_stride = 1;
		self->mip_levels   = 1;
		self->array_layers = 1;
		self->block_dim_x  = 1;
		self->block_dim_y  = 1;
	}

	static void tfl_set_1d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t array_layers, uint32_t mip_levels);
	static void tfl_set_2d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t height, uint32_t array_layers, uint32_t mip_levels);
	static void tfl_set_3d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels);

	static uint32_t tfl_format_block_size(VkFormat format);
	static void tfl_format_block_dim(VkFormat format, uint32_t *width, uint32_t *height);
	static uint32_t tfl_num_miplevels(uint32_t width, uint32_t height, uint32_t depth);

	static inline void tfl_set_buffer(struct TextureFormatLayout *self, void *buffer, size_t size)
	{
		self->buffer = (uint8_t *)(buffer);
		self->buffer_size = size;
	}
	static inline void *tfl_get_buffer(struct TextureFormatLayout *self) { return self->buffer; }
	static inline uint32_t tfl_get_width(const struct TextureFormatLayout *self, uint32_t mip) { return self->mips[mip].width; }
	static inline uint32_t tfl_get_height(const struct TextureFormatLayout *self, uint32_t mip) { return self->mips[mip].height; }
	static inline uint32_t tfl_get_depth(const struct TextureFormatLayout *self, uint32_t mip) { return self->mips[mip].depth; }
	static inline VkFormat tfl_get_format(const struct TextureFormatLayout *self) { return self->format; }
	static inline size_t tfl_get_required_size(const struct TextureFormatLayout *self) { return self->required_size; }
	static inline size_t tfl_row_byte_stride(const struct TextureFormatLayout *self, uint32_t row_length)
	{
		return ((row_length + self->block_dim_x - 1) / self->block_dim_x) * self->block_stride;
	}
	static inline size_t tfl_layer_byte_stride(const struct TextureFormatLayout *self, uint32_t image_height, size_t row_byte_stride)
	{
		return ((image_height + self->block_dim_y - 1) / self->block_dim_y) * row_byte_stride;
	}
	static inline size_t tfl_get_row_size(const struct TextureFormatLayout *self, uint32_t mip)
	{
		return self->mips[mip].block_row_length * self->block_stride;
	}
	static inline size_t tfl_get_layer_size(const struct TextureFormatLayout *self, uint32_t mip)
	{
		return self->mips[mip].block_image_height * tfl_get_row_size(self, mip);
	}
	static inline const TextureFormatLayoutMipInfo *tfl_get_mip_info(const struct TextureFormatLayout *self, uint32_t mip)
	{
		return &self->mips[mip];
	}
	static inline void *tfl_data(const struct TextureFormatLayout *self, uint32_t layer, uint32_t mip)
	{
		const TextureFormatLayoutMipInfo *mip_info;
		uint8_t *slice;
		assert(self->buffer);
		assert(self->buffer_size == self->required_size);
		mip_info = &self->mips[mip];
		slice = self->buffer + mip_info->offset;
		slice += self->block_stride * layer * mip_info->block_row_length * mip_info->block_image_height;
		return slice;
	}
	static void tfl_build_buffer_image_copies(const struct TextureFormatLayout *self, VkBufferImageCopy *copies, unsigned *num_copies);
	static void tfl_fill_mipinfo(struct TextureFormatLayout *self, uint32_t width, uint32_t height, uint32_t depth);

	class Device;

	/* Hash-identity base. Converted from a class-with-ctor to a plain C89 struct;
	 * derived pointees now call cookie_init() in their own constructor body
	 * instead of inheriting the Cookie(Device*) ctor. The single uint64_t field
	 * is still inherited, so the many obj.cookie / obj->cookie reads are
	 * unchanged. cookie_init is defined out-of-line (below Device) because it
	 * calls Device::allocate_cookie(). */
	struct Cookie
	{
		uint64_t cookie;
	};

/* ============================================================
 * format.hpp
 * ============================================================ */

/* Pure-VkFormat predicates and the aspect-mask classifier are all simple
 * switch statements; they have no C++ dependencies and could be lowered to C
 * verbatim after a future namespace-removal pass. The TextureFormatLayout-
 * dependent helpers below are kept separate and remain C++-only. */
static inline bool format_has_depth_aspect(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return true;

		default:
			break;
	}
	return false;
}

static inline bool format_has_stencil_aspect(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
		case VK_FORMAT_S8_UINT:
			return true;

		default:
			break;
	}
	return false;
}

static inline bool format_has_depth_or_stencil_aspect(VkFormat format)
{
	return format_has_depth_aspect(format) || format_has_stencil_aspect(format);
}

static inline VkImageAspectFlags format_to_aspect_mask(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_UNDEFINED:
			return 0;

		case VK_FORMAT_S8_UINT:
			return VK_IMAGE_ASPECT_STENCIL_BIT;

		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
			return VK_IMAGE_ASPECT_DEPTH_BIT;

		default:
			break;
	}
	return VK_IMAGE_ASPECT_COLOR_BIT;
}

/* ============================================================
 * sampler.hpp
 * ============================================================ */

	enum StockSampler {
		StockSampler_NearestClamp,
		StockSampler_LinearClamp,
		StockSampler_TrilinearClamp,
		StockSampler_NearestWrap,
		StockSampler_LinearWrap,
		StockSampler_TrilinearWrap,
		StockSampler_NearestShadow,
		StockSampler_LinearShadow,
		StockSampler_Count
	};

	struct SamplerCreateInfo
	{
		VkFilter mag_filter;
		VkFilter min_filter;
		VkSamplerMipmapMode mipmap_mode;
		VkSamplerAddressMode address_mode_u;
		VkSamplerAddressMode address_mode_v;
		VkSamplerAddressMode address_mode_w;
		float mip_lod_bias;
		VkBool32 anisotropy_enable;
		float max_anisotropy;
		VkBool32 compare_enable;
		VkCompareOp compare_op;
		float min_lod;
		float max_lod;
		VkBorderColor border_color;
		VkBool32 unnormalized_coordinates;
	};

	struct Sampler;
	struct SamplerDeleter
	{
		void operator()(Sampler *sampler);
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). The Cookie
	 * base, which provides the hash identity, is unrelated and stays. */
	/* Sampler: plain C struct + free functions (was a class deriving Cookie with
	 * refcount methods + ctor/dtor). Cookie is embedded as the first member
	 * (composition replacing inheritance); sampler_init seeds it via cookie_init.
	 * Pooled via object_pool_raw; the refcount starts at 1. */
	struct Sampler
	{
		struct Cookie cookie_base; /* was: public Cookie base subobject */
		Device *device;
		VkSampler sampler;
		HandleCounter reference_count;
	};
	static void sampler_init(struct Sampler *self, Device *device, VkSampler sampler);
	void sampler_fini(struct Sampler *self);
	static inline VkSampler sampler_get_sampler(const struct Sampler *self) { return self->sampler; }
	static inline void sampler_add_reference(struct Sampler *self) { counter_add_ref(&self->reference_count); }
	static void sampler_release_reference(struct Sampler *self);
	/* Sampler handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(SamplerHandle,
	 * Sampler) to a plain struct + explicit free functions, following the
	 * sem_ / fence_ / bvh_ template. The pointee Sampler is unchanged. Stored in a
	 * fixed device-lifetime array (samplers[StockSampler_Count]); no growing
	 * container, so no realloc accounting -- assignment into a slot is a plain
	 * struct copy that transfers the producer's refcount-1, teardown is an
	 * explicit smh_reset() per slot.
	 * ASAN-GATE: stock samplers persist for the device lifetime; verify the per-
	 * slot reset in deinit drops exactly one ref (no leak / no double-free). */
	struct SamplerHandle { Sampler *data; };
	static inline struct SamplerHandle smh_make(Sampler *p) { struct SamplerHandle h; h.data = p; return h; }
	static inline void smh_reset(struct SamplerHandle *h) { if (h->data) sampler_release_reference(h->data); h->data = NULL; }
	static inline Sampler *smh_get(const struct SamplerHandle *h) { return h->data; }
	static inline int smh_is_valid(const struct SamplerHandle *h) { return h->data != NULL; }

/* ============================================================
 * memory_allocator.hpp
 * ============================================================ */

#ifndef FRAMEWORK_MEMORY_ALLOCATOR_HPP
#define FRAMEWORK_MEMORY_ALLOCATOR_HPP

static inline uint32_t log2_integer(uint32_t v)
{
	v--;
	return 32 - leading_zeroes(v);
}

enum MemoryClass
{
	MEMORY_CLASS_SMALL = 0,
	MEMORY_CLASS_MEDIUM,
	MEMORY_CLASS_LARGE,
	MEMORY_CLASS_HUGE,
	MEMORY_CLASS_COUNT
};

enum AllocationTiling
{
	ALLOCATION_TILING_LINEAR = 0,
	ALLOCATION_TILING_OPTIMAL,
	ALLOCATION_TILING_COUNT
};

enum MemoryAccessFlag
{
	MEMORY_ACCESS_WRITE_BIT = 1,
	MEMORY_ACCESS_READ_BIT = 2
};
using MemoryAccessFlags = uint32_t;

struct DeviceAllocation;
struct DeviceAllocator;

/* Block sub-allocator constants, hoisted from the former Block:: anonymous enum
 * to file scope so the class->struct conversion carries no nested type. */
enum { BLOCK_NUM_SUB_BLOCKS = 32u };
#define BLOCK_ALL_FREE (~0u)

/* Block: 32-way sub-block bitmap allocator. Converted from a C++ class (deleted
 * copy, default ctor that fills the free bitmap, dtor that leak-checks) to a
 * plain C struct + free functions. Embedded by value in MiniHeap; block_init
 * seeds it (was the default ctor), block_fini runs the leak check (was the
 * dtor). */
struct Block
{
	uint32_t free_blocks[BLOCK_NUM_SUB_BLOCKS];
	uint32_t longest_run;
};

static inline void block_init(struct Block *self)
{
	unsigned i;
	for (i = 0; i < BLOCK_NUM_SUB_BLOCKS; i++)
		self->free_blocks[i] = BLOCK_ALL_FREE;
	self->longest_run = 32;
}

static inline void block_fini(struct Block *self)
{
	if (self->free_blocks[0] != BLOCK_ALL_FREE)
		LOGE("Memory leak in block detected.\n");
}

static inline bool block_full(const struct Block *self)
{
	return self->free_blocks[0] == 0;
}

static inline bool block_empty(const struct Block *self)
{
	return self->free_blocks[0] == BLOCK_ALL_FREE;
}

static inline uint32_t block_get_longest_run(const struct Block *self)
{
	return self->longest_run;
}

static inline void block_update_longest_run(struct Block *self)
{
	uint32_t f = self->free_blocks[0];
	self->longest_run = 0;

	while (f)
	{
		self->free_blocks[self->longest_run++] = f;
		f &= f >> 1;
	}
}

void block_allocate(struct Block *self, uint32_t num_blocks, DeviceAllocation *block);
void block_free(struct Block *self, uint32_t mask);

struct MiniHeap;
struct ClassAllocator;
struct DeviceAllocator;
struct Allocator;

/* DeviceAllocation: plain bookkeeping value. Converted from a struct-with-methods
 * to a plain C struct + deviceallocation_* free functions. It is always brace-
 * initialized to {} at its allocation sites, so the former default member
 * initializers (all NULL/0/false) are unnecessary. The two free_immediate
 * overloads split by name: the 0-arg recycle path is deviceallocation_free_immediate,
 * the DeviceAllocator* variant is deviceallocation_free_immediate_alloc. */
struct DeviceAllocation
{
	VkDeviceMemory base;
	uint8_t *host_base;
	struct ClassAllocator *alloc;
	struct MiniHeap *heap;
	uint32_t offset;
	uint32_t mask;
	uint32_t size;

	uint8_t tiling;
	uint8_t memory_type;
	bool hierarchical;
};

static inline VkDeviceMemory deviceallocation_get_memory(const struct DeviceAllocation *self) { return self->base; }
static inline uint32_t deviceallocation_get_offset(const struct DeviceAllocation *self) { return self->offset; }
static inline uint32_t deviceallocation_get_size(const struct DeviceAllocation *self) { return self->size; }
void deviceallocation_free_immediate(struct DeviceAllocation *self);
void deviceallocation_free_immediate_alloc(struct DeviceAllocation *self, DeviceAllocator *allocator);
void deviceallocation_free_global(struct DeviceAllocation *self, DeviceAllocator *allocator, uint32_t size, uint32_t memory_type);

/* Owning array of (trivially copyable) DeviceAllocation. Replaces
 * std::vector<DeviceAllocation> for a frame's deferred-free list. DeviceAllocation
 * is a trivially relocatable bookkeeping value (handles, an intrusive-list
 * iterator, and PODs - it frees its backing memory only via an explicit
 * free_immediate(), not a destructor), so growth is a realloc and clear() just
 * resets the count; no per-element construction or destruction. push() copy-
 * appends; the frame drains the list with a range-for then clear(). */
struct DeviceAllocationVec
{
	DeviceAllocation *items;
	int count;
	int cap;
};
static inline void DeviceAllocationVec_init(struct DeviceAllocationVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
static inline void DeviceAllocationVec_free_storage(struct DeviceAllocationVec *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; }
static inline int  DeviceAllocationVec_size(const struct DeviceAllocationVec *v) { return v->count; }
static inline int  DeviceAllocationVec_empty(const struct DeviceAllocationVec *v) { return v->count == 0; }
static inline void DeviceAllocationVec_clear(struct DeviceAllocationVec *v) { v->count = 0; }
static inline DeviceAllocation *DeviceAllocationVec_at(struct DeviceAllocationVec *v, int i) { return &v->items[i]; }
static inline void DeviceAllocationVec_push(struct DeviceAllocationVec *v, const DeviceAllocation *valp) {
	if (v->count >= v->cap) {
		int ncap = v->cap ? v->cap * 2 : 8;
		DeviceAllocation *nitems = (DeviceAllocation *)realloc(v->items, (size_t)ncap * sizeof(DeviceAllocation));
		if (!nitems)
			return;
		v->items = nitems;
		v->cap = ncap;
	}
	v->items[v->count++] = *valp;
}

struct MiniHeap
{
	struct IntrusiveListNode list_node; /* must stay first (offset 0) */
	DeviceAllocation allocation;
	Block heap;
};

/* The concrete list recovers a MiniHeap* from an IntrusiveListNode* by casting,
 * which is only valid if the node base is at offset 0. */
static_assert(offsetof(MiniHeap, list_node) == 0,
		"MiniHeap.list_node must be the first member for intrusive-list casts");

struct Allocator;

/* AllocationTilingHeaps hoisted out of ClassAllocator (was a nested type) to file
 * scope as ClassAllocatorTilingHeaps. */
struct ClassAllocatorTilingHeaps
{
	struct IntrusiveListC heaps[BLOCK_NUM_SUB_BLOCKS];
	struct IntrusiveListC full_heaps;
	uint32_t heap_availability_mask;
};

/* ClassAllocator: per-size-class sub-allocator. Converted from a C++ class to a
 * plain C struct + classallocator_* free functions. The private default ctor
 * (init the MiniHeap pool) becomes classallocator_init; the dtor (leak check +
 * pool deinit) becomes classallocator_fini. heap_availability_mask and the
 * scalar fields are seeded in classallocator_init (no in-struct initializers in
 * a plain C struct). */
struct ClassAllocator
{
	struct ClassAllocator *parent;
	struct ClassAllocatorTilingHeaps tiling_modes[ALLOCATION_TILING_COUNT];
	struct ObjectPoolRaw object_pool;

	uint32_t sub_block_size;
	uint32_t sub_block_size_log2;
	uint32_t tiling_mask;
	uint32_t memory_type;
	DeviceAllocator *global_allocator;
};

void classallocator_init(struct ClassAllocator *self);
void classallocator_fini(struct ClassAllocator *self);
bool classallocator_allocate(struct ClassAllocator *self, uint32_t size, AllocationTiling tiling, struct DeviceAllocation *alloc, bool hierarchical);
void classallocator_free(struct ClassAllocator *self, struct DeviceAllocation *alloc);
void classallocator_suballocate(struct ClassAllocator *self, uint32_t num_blocks, uint32_t tiling, uint32_t memory_type, struct MiniHeap *heap, struct DeviceAllocation *alloc);

static inline void classallocator_set_tiling_mask(struct ClassAllocator *self, uint32_t mask) { self->tiling_mask = mask; }
static inline void classallocator_set_sub_block_size(struct ClassAllocator *self, uint32_t size)
{
	self->sub_block_size_log2 = log2_integer(size);
	self->sub_block_size = size;
}
static inline void classallocator_set_global_allocator(struct ClassAllocator *self, DeviceAllocator *allocator) { self->global_allocator = allocator; }
static inline void classallocator_set_memory_type(struct ClassAllocator *self, uint32_t type) { self->memory_type = type; }
static inline void classallocator_set_parent(struct ClassAllocator *self, struct ClassAllocator *allocator) { self->parent = allocator; }

/* Allocator: per-memory-type front end owning one ClassAllocator per size class.
 * Converted from a C++ class to a plain C struct + alloc_* free functions. The
 * range-for loops over the by-value classes[] array become index loops. The
 * former static free() (which just forwarded to DeviceAllocation::free_immediate)
 * becomes alloc_free. ClassAllocator must be a complete type above this point
 * because classes[] is by value. */
struct Allocator
{
	struct ClassAllocator classes[MEMORY_CLASS_COUNT];
	DeviceAllocator *global_allocator;
	uint32_t memory_type;
};

void alloc_init(struct Allocator *self);
bool alloc_allocate(struct Allocator *self, uint32_t size, uint32_t alignment, AllocationTiling tiling, struct DeviceAllocation *alloc);
bool alloc_allocate_global(struct Allocator *self, uint32_t size, struct DeviceAllocation *alloc);
bool alloc_allocate_dedicated(struct Allocator *self, uint32_t size, struct DeviceAllocation *alloc, VkImage image);

static inline struct ClassAllocator *alloc_get_class_allocator(struct Allocator *self, MemoryClass clazz) { return &self->classes[(unsigned)(clazz)]; }
static inline void alloc_free(struct DeviceAllocation *alloc) { deviceallocation_free_immediate(alloc); }
static inline void alloc_set_memory_type(struct Allocator *self, uint32_t memory_type)
{
	unsigned i;
	for (i = 0; i < MEMORY_CLASS_COUNT; i++)
		classallocator_set_memory_type(&self->classes[i], memory_type);
	self->memory_type = memory_type;
}
static inline void alloc_set_global_allocator(struct Allocator *self, DeviceAllocator *allocator)
{
	unsigned i;
	for (i = 0; i < MEMORY_CLASS_COUNT; i++)
		classallocator_set_global_allocator(&self->classes[i], allocator);
	self->global_allocator = allocator;
}

/* Owning array of Allocator* (one per memory type). Replaces
 * std::vector<std::unique_ptr<Allocator>>: the container owns each heap-allocated
 * Allocator and deletes it on clear()/destruction, so ownership is explicit
 * new/delete instead of unique_ptr. The pointers themselves are trivially
 * relocatable, so the backing array grows by realloc. push() takes ownership of
 * an already-new'd Allocator; operator[]/back() return the raw pointer for the
 * ->allocate() calls. ::malloc/::free are qualified because the surrounding code
 * has free() members in scope. */
struct AllocatorPtrVec
{
	Allocator **items;
	int count;
	int cap;
};
static inline void AllocatorPtrVec_init(struct AllocatorPtrVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
/* Takes ownership of an already-allocated Allocator. */
static inline void AllocatorPtrVec_push(struct AllocatorPtrVec *v, Allocator *p) {
	if (v->count >= v->cap) {
		int ncap = v->cap ? v->cap * 2 : 8;
		Allocator **nitems = (Allocator **)realloc(v->items, (size_t)ncap * sizeof(Allocator *));
		if (!nitems)
			return;
		v->items = nitems;
		v->cap = ncap;
	}
	v->items[v->count++] = p;
}
static inline int  AllocatorPtrVec_size(const struct AllocatorPtrVec *v) { return v->count; }
static inline Allocator *AllocatorPtrVec_back(const struct AllocatorPtrVec *v) { return v->items[v->count - 1]; }
static inline void AllocatorPtrVec_clear(struct AllocatorPtrVec *v) {
	int i;
	for (i = 0; i < v->count; i++)
		free(v->items[i]);
	v->count = 0;
}
static inline void AllocatorPtrVec_destroy(struct AllocatorPtrVec *v) {
	int i;
	for (i = 0; i < v->count; i++)
		free(v->items[i]);
	free(v->items);
	v->items = NULL; v->count = 0; v->cap = 0;
}

/* --- DeviceAllocator's former nested types, hoisted to file scope --- */

struct DeviceAllocatorAllocation
{
	VkDeviceMemory memory;
	uint8_t *host_memory;
	uint32_t size;
	uint32_t type;
};

/* Owning array of (POD) DeviceAllocatorAllocation (was DeviceAllocator::Allocation).
 * Trivially relocatable, so growth is a realloc and erase is a memmove shift. */
struct DeviceAllocatorAllocationVec
{
	struct DeviceAllocatorAllocation *items;
	int count;
	int cap;
};
static inline void da_alloc_vec_init(struct DeviceAllocatorAllocationVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
static inline void da_alloc_vec_free_storage(struct DeviceAllocatorAllocationVec *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; }
static inline int  da_alloc_vec_size(const struct DeviceAllocatorAllocationVec *v) { return v->count; }
static inline int  da_alloc_vec_empty(const struct DeviceAllocatorAllocationVec *v) { return v->count == 0; }
static inline struct DeviceAllocatorAllocation *da_alloc_vec_at(struct DeviceAllocatorAllocationVec *v, int i) { return &v->items[i]; }
static inline void da_alloc_vec_push(struct DeviceAllocatorAllocationVec *v, const struct DeviceAllocatorAllocation *valp) {
	if (v->count >= v->cap) {
		int ncap = v->cap ? v->cap * 2 : 8;
		struct DeviceAllocatorAllocation *nitems = (struct DeviceAllocatorAllocation *)realloc(v->items, (size_t)ncap * sizeof(struct DeviceAllocatorAllocation));
		if (!nitems)
			return;
		v->items = nitems;
		v->cap = ncap;
	}
	v->items[v->count++] = *valp;
}
static inline void da_alloc_vec_erase_at(struct DeviceAllocatorAllocationVec *v, int i) {
	memmove(&v->items[i], &v->items[i + 1], (size_t)(v->count - i - 1) * sizeof(struct DeviceAllocatorAllocation));
	v->count--;
}
static inline void da_alloc_vec_erase_front(struct DeviceAllocatorAllocationVec *v, int k) {
	if (k <= 0)
		return;
	memmove(&v->items[0], &v->items[k], (size_t)(v->count - k) * sizeof(struct DeviceAllocatorAllocation));
	v->count -= k;
}

struct DeviceAllocatorHeap
{
	uint64_t size;
	struct DeviceAllocatorAllocationVec blocks;
};
static inline void da_heap_init(struct DeviceAllocatorHeap *h) { h->size = 0; da_alloc_vec_init(&h->blocks); }
static inline void da_heap_destroy(struct DeviceAllocatorHeap *h) { da_alloc_vec_free_storage(&h->blocks); }
/* Move src into dst (steal blocks), leaving src empty. */
static inline void da_heap_move(struct DeviceAllocatorHeap *dst, struct DeviceAllocatorHeap *src) {
	dst->size = src->size;
	dst->blocks = src->blocks;
	da_alloc_vec_init(&src->blocks);
}
void da_heap_garbage_collect(struct DeviceAllocatorHeap *self, VkDevice device);

/* Owning array of DeviceAllocatorHeap (was DeviceAllocator::HeapVec). The C++
 * move-only RAII container is now a plain struct + da_heap_vec_* free functions;
 * growth da_heap_moves each element into new storage and destroys the moved-from
 * slot, resize default-constructs new tail elements (growing) or destroys the
 * tail (shrinking). */
struct DeviceAllocatorHeapVec
{
	struct DeviceAllocatorHeap *items;
	int count;
	int cap;
};
static inline void da_heap_vec_init(struct DeviceAllocatorHeapVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
static inline int  da_heap_vec_size(const struct DeviceAllocatorHeapVec *v) { return v->count; }
static inline struct DeviceAllocatorHeap *da_heap_vec_at(struct DeviceAllocatorHeapVec *v, int i) { return &v->items[i]; }
static inline void da_heap_vec_clear(struct DeviceAllocatorHeapVec *v) {
	int i;
	for (i = 0; i < v->count; i++)
		da_heap_destroy(&v->items[i]);
	v->count = 0;
}
static inline void da_heap_vec_grow(struct DeviceAllocatorHeapVec *v, int ncap) {
	struct DeviceAllocatorHeap *nitems = (struct DeviceAllocatorHeap *)malloc((size_t)ncap * sizeof(struct DeviceAllocatorHeap));
	int i;
	for (i = 0; i < v->count; i++) {
		da_heap_move(&nitems[i], &v->items[i]);
		da_heap_destroy(&v->items[i]);
	}
	free(v->items);
	v->items = nitems;
	v->cap = ncap;
}
static inline void da_heap_vec_resize(struct DeviceAllocatorHeapVec *v, int n) {
	int i;
	if (n > v->count) {
		if (n > v->cap)
			da_heap_vec_grow(v, n);
		for (i = v->count; i < n; i++)
			da_heap_init(&v->items[i]);
	} else {
		for (i = n; i < v->count; i++)
			da_heap_destroy(&v->items[i]);
	}
	v->count = n;
}
static inline void da_heap_vec_free_storage(struct DeviceAllocatorHeapVec *v) {
	da_heap_vec_clear(v);
	free(v->items);
	v->items = NULL; v->count = 0; v->cap = 0;
}

/* DeviceAllocator: top of the memory-allocator hierarchy. Converted from a C++
 * class to a plain C struct + deviceallocator_* free functions. The malloc'd
 * owner (Device) drives it via deviceallocator_init_empty / _deinit; the real
 * setup is deviceallocator_init(gpu, device). */
struct DeviceAllocator
{
	struct AllocatorPtrVec allocators;
	VkDevice device;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkDeviceSize atom_alignment;
	bool use_dedicated;
	struct DeviceAllocatorHeapVec heaps;
};

void deviceallocator_init(struct DeviceAllocator *self, VkPhysicalDevice gpu, VkDevice device);
bool deviceallocator_allocate_image_memory(struct DeviceAllocator *self, uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling, struct DeviceAllocation *alloc, VkImage image);
void deviceallocator_garbage_collect(struct DeviceAllocator *self);
void *deviceallocator_map_memory(struct DeviceAllocator *self, const struct DeviceAllocation *alloc, MemoryAccessFlags flags);
void deviceallocator_unmap_memory(struct DeviceAllocator *self, const struct DeviceAllocation *alloc, MemoryAccessFlags flags);
bool deviceallocator_allocate(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory, VkImage dedicated_image);
void deviceallocator_free(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);
void deviceallocator_free_no_recycle(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);

static inline void deviceallocator_init_empty(struct DeviceAllocator *self)
{
	self->allocators.items = NULL;
	self->allocators.count = 0;
	self->allocators.cap   = 0;
	self->heaps.items = NULL;
	self->heaps.count = 0;
	self->heaps.cap   = 0;
	self->device         = VK_NULL_HANDLE;
	self->atom_alignment = 1;
	self->use_dedicated  = false;
}

static inline void deviceallocator_deinit(struct DeviceAllocator *self)
{
	int i;
	for (i = 0; i < self->heaps.count; i++)
		da_heap_garbage_collect(&self->heaps.items[i], self->device);
	AllocatorPtrVec_destroy(&self->allocators);
	da_heap_vec_free_storage(&self->heaps);
}

static inline void deviceallocator_set_supports_dedicated_allocation(struct DeviceAllocator *self, bool enable)
{
	self->use_dedicated = enable;
}

static inline bool deviceallocator_allocate_typed(struct DeviceAllocator *self, uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling, struct DeviceAllocation *alloc)
{
	return alloc_allocate(self->allocators.items[memory_type], size, alignment, tiling, alloc);
}

static inline bool deviceallocator_allocate_global(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, struct DeviceAllocation *alloc)
{
	return alloc_allocate_global(self->allocators.items[memory_type], size, alloc);
}

#endif



	class Device;

	static inline VkPipelineStageFlags buffer_usage_to_possible_stages(VkBufferUsageFlags usage)
	{
		VkPipelineStageFlags flags = 0;
		if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
			flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
			flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
			flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
					VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		return flags;
	}

	static inline VkAccessFlags buffer_usage_to_possible_access(VkBufferUsageFlags usage)
	{
		VkAccessFlags flags = 0;
		if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
			flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
			flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
			flags |= VK_ACCESS_INDEX_READ_BIT;
		if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
			flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
			flags |= VK_ACCESS_UNIFORM_READ_BIT;
		if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

		return flags;
	}

	enum BufferDomain {
		BufferDomain_Device, // BufferDomain_Device local. Probably not visible from CPU.
		BufferDomain_Host, // BufferDomain_Host-only, needs to be synced to GPU. Might be device local as well on iGPUs.
		BufferDomain_CachedHost // BufferDomain_Host-only, used for readbacks.
	};

	struct BufferCreateInfo
	{
		BufferDomain domain = BufferDomain_Device;
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = 0;
	};

	struct Buffer;
	struct BufferDeleter
	{
		void operator()(Buffer *buffer);
	};

	struct BufferView;
	struct BufferViewDeleter
	{
		void operator()(BufferView *view);
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). The Cookie
	 * base, which provides the hash identity, is unrelated and stays. */
	/* Buffer: plain C struct + free functions (was a class deriving Cookie).
	 * Cookie embedded as cookie_base. Pooled; refcount starts at 1. */
	struct Buffer
	{
		struct Cookie cookie_base;
		Device *device;
		VkBuffer buffer;
		DeviceAllocation alloc;
		BufferCreateInfo info;
		HandleCounter reference_count;
	};
	static void buffer_init(struct Buffer *self, Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info);
	void buffer_fini(struct Buffer *self);
	static inline VkBuffer buffer_get_buffer(const struct Buffer *self) { return self->buffer; }
	static inline const BufferCreateInfo &buffer_get_create_info(const struct Buffer *self) { return self->info; }
	static inline const DeviceAllocation &buffer_get_allocation(const struct Buffer *self) { return self->alloc; }
	static inline void buffer_add_reference(struct Buffer *self) { counter_add_ref(&self->reference_count); }
	static void buffer_release_reference(struct Buffer *self);
	/* Buffer handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(BufferHandle, Buffer)
	 * to a plain struct + explicit free functions, following the established
	 * template. The pointee Buffer is unchanged. This is the last RAII handle and
	 * the most pool-entangled: BufferBlock holds gpu/cpu handle members (with a
	 * user-declared ~BufferBlock), the copy-insert BufferBlockVec, plus
	 * InitialImageBuffer.buffer and Renderer.quad members. Helpers mirror the
	 * ImageHandle set: bh_make (wrap produced ref), bh_reset (decref+null),
	 * bh_get, bh_is_valid, bh_copy (retain), bh_assign (release-old/retain-new),
	 * bh_move (release-old/take-produced), bh_steal (move).
	 * ASAN-GATE: buffer-pool block recycle + per-frame staging buffer lifetime. */
	struct BufferHandle { Buffer *data; };
	static inline struct BufferHandle bh_make(Buffer *p) { struct BufferHandle h; h.data = p; return h; }
	static inline void bh_reset(struct BufferHandle *h) { if (h->data) buffer_release_reference(h->data); h->data = NULL; }
	static inline Buffer *bh_get(const struct BufferHandle *h) { return h->data; }
	static inline int bh_is_valid(const struct BufferHandle *h) { return h->data != NULL; }
	static inline void bh_copy(struct BufferHandle *dst, const struct BufferHandle *src) {
		dst->data = src->data;
		if (dst->data) buffer_add_reference(dst->data);
	}
	static inline void bh_assign(struct BufferHandle *dst, const struct BufferHandle *src) {
		if (dst == src) return;
		if (src->data) buffer_add_reference(src->data);
		if (dst->data) buffer_release_reference(dst->data);
		dst->data = src->data;
	}
	static inline void bh_move(struct BufferHandle *dst, struct BufferHandle produced) {
		if (dst->data) buffer_release_reference(dst->data);
		dst->data = produced.data;
	}
	static inline void bh_steal(struct BufferHandle *dst, struct BufferHandle *src) { dst->data = src->data; src->data = NULL; }

	struct BufferViewCreateInfo
	{
		const Buffer *buffer;
		VkFormat format;
		VkDeviceSize offset;
		VkDeviceSize range;
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). The Cookie
	 * base, which provides the hash identity, is unrelated and stays. */
	/* BufferView: plain C struct + free functions (was a class deriving Cookie).
	 * Cookie embedded as the first member (cookie_base). Pooled; refcount starts
	 * at 1. info.buffer is a raw const Buffer* (not a handle), so get_buffer
	 * returns it by reference unchanged. */
	struct BufferView
	{
		struct Cookie cookie_base;
		Device *device;
		VkBufferView view;
		BufferViewCreateInfo info;
		HandleCounter reference_count;
	};
	static void bufferview_init(struct BufferView *self, Device *device, VkBufferView view, const BufferViewCreateInfo &info);
	void bufferview_fini(struct BufferView *self);
	static inline VkBufferView bufferview_get_view(const struct BufferView *self) { return self->view; }
	static inline const Buffer &bufferview_get_buffer(const struct BufferView *self) { return *self->info.buffer; }
	static inline void bufferview_add_reference(struct BufferView *self) { counter_add_ref(&self->reference_count); }
	static void bufferview_release_reference(struct BufferView *self);
	/* Plain-C form of the BufferView intrusive handle (was
	 * INTRUSIVE_HANDLE_DECLARE(BufferViewHandle, BufferView)). Same semantics:
	 * construct from a raw BufferView* that already carries one reference, reset
	 * releases one. The handle is only ever created, dereferenced and dropped at
	 * the two call sites (never copied or stored), so only make/reset/get are
	 * provided; lifecycle is driven explicitly by the bvh_* helpers. */
	struct BufferViewHandle {
		BufferView *data;
	};
	static inline struct BufferViewHandle bvh_make(BufferView *p) { struct BufferViewHandle h; h.data = p; return h; }
	static inline void bvh_reset(struct BufferViewHandle *h) { if (h->data) bufferview_release_reference(h->data); h->data = NULL; }
	static inline BufferView *bvh_get(struct BufferViewHandle *h) { return h->data; }

	class Device;

	static inline VkPipelineStageFlags image_usage_to_possible_stages(VkImageUsageFlags usage)
	{
		VkPipelineStageFlags flags = 0;

		if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
		{
			VkPipelineStageFlags possible = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
				possible |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

			flags &= possible;
		}

		return flags;
	}

	static inline VkAccessFlags image_layout_to_possible_access(VkImageLayout layout)
	{
		switch (layout)
		{
			case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
				return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
				return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
				return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
				return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
				return VK_ACCESS_TRANSFER_READ_BIT;
			case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
				return VK_ACCESS_TRANSFER_WRITE_BIT;
			default:
				return ~0u;
		}
	}

	static inline VkAccessFlags image_usage_to_possible_access(VkImageUsageFlags usage)
	{
		VkAccessFlags flags = 0;

		if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_ACCESS_SHADER_READ_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
			flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

		// Transient attachments can only be attachments, and never other resources.
		if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
		{
			flags &= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}

		return flags;
	}

	static inline uint32_t image_num_miplevels(const VkExtent3D &extent)
	{
		uint32_t wh = (extent.width > extent.height) ? extent.width : extent.height;
		uint32_t size = (wh > extent.depth) ? wh : extent.depth;
		uint32_t levels = 0;
		while (size)
		{
			levels++;
			size >>= 1;
		}
		return levels;
	}

	static inline VkFormatFeatureFlags image_usage_to_features(VkImageUsageFlags usage)
	{
		VkFormatFeatureFlags flags = 0;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

		return flags;
	}

	struct ImageInitialData
	{
		const void *data;
		unsigned row_length;
		unsigned image_height;
	};

	enum ImageMiscFlagBits
	{
		IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0
	};
	using ImageMiscFlags = uint32_t;

	struct Image;

	struct ImageViewCreateInfo
	{
		Image *image = NULL;
		VkFormat format = VK_FORMAT_UNDEFINED;
		unsigned base_level = 0;
		unsigned levels = VK_REMAINING_MIP_LEVELS;
		unsigned base_layer = 0;
		unsigned layers = VK_REMAINING_ARRAY_LAYERS;
		VkComponentMapping swizzle = {
			VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
		};
	};

	struct ImageView;

	struct ImageViewDeleter
	{
		void operator()(ImageView *view);
	};

	POD_VEC_DECLARE(RenderTargetViewVec, VkImageView);

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). The Cookie
	 * base, which provides the hash identity, is unrelated and stays. */
	/* ImageView: plain C struct + free functions (was a class deriving Cookie).
	 * Cookie embedded as cookie_base. Pooled; refcount starts at 1. The setters
	 * (set_alt_views / set_render_target_views) and the several view getters
	 * become free functions; get_render_target_view stays out-of-line. */
	struct ImageView
	{
		struct Cookie cookie_base;
		Device *device;
		VkImageView view;
		RenderTargetViewVec render_target_views;
		VkImageView depth_view;
		VkImageView stencil_view;
		ImageViewCreateInfo info;
		HandleCounter reference_count;
	};
	static void imageview_init(struct ImageView *self, Device *device, VkImageView view, const ImageViewCreateInfo &info);
	void imageview_fini(struct ImageView *self);
	static VkImageView imageview_get_render_target_view(const struct ImageView *self, unsigned layer);
	static inline void imageview_set_alt_views(struct ImageView *self, VkImageView depth, VkImageView stencil)
	{
		VK_ASSERT(self->depth_view == VK_NULL_HANDLE);
		VK_ASSERT(self->stencil_view == VK_NULL_HANDLE);
		self->depth_view = depth;
		self->stencil_view = stencil;
	}
	static inline void imageview_set_render_target_views(struct ImageView *self, RenderTargetViewVec views)
	{
		VK_ASSERT(RenderTargetViewVec_empty(&self->render_target_views));
		/* POD_VEC passed by value: adopt its backing array (the caller nulls its
		 * own copy so ownership transfers cleanly, mirroring the old move). */
		self->render_target_views = views;
	}
	static inline VkImageView imageview_get_view(const struct ImageView *self) { return self->view; }
	static inline VkImageView imageview_get_float_view(const struct ImageView *self) { return self->depth_view != VK_NULL_HANDLE ? self->depth_view : self->view; }
	static inline VkImageView imageview_get_integer_view(const struct ImageView *self) { return self->stencil_view != VK_NULL_HANDLE ? self->stencil_view : self->view; }
	static inline VkFormat imageview_get_format(const struct ImageView *self) { return self->info.format; }
	static inline const Image &imageview_get_image_const(const struct ImageView *self) { return *self->info.image; }
	static inline Image &imageview_get_image(struct ImageView *self) { return *self->info.image; }
	static inline const ImageViewCreateInfo &imageview_get_create_info(const struct ImageView *self) { return self->info; }
	static inline void imageview_add_reference(struct ImageView *self) { counter_add_ref(&self->reference_count); }
	static void imageview_release_reference(struct ImageView *self);

	/* Image-view handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(ImageViewHandle,
	 * ImageView) to a plain struct + explicit free functions, following the
	 * sem_ / fence_ / smh_ / bvh_ template. The pointee ImageView is unchanged.
	 * The container below (ImageViewHandleVec) is move-only -- push() takes an
	 * rvalue and steals, grow() moves -- so there is no incref anywhere in the
	 * container; iv_reset() (one decref) is needed only at destroy()/clear() and
	 * at value-member teardown. iv_steal() implements the move (copy pointer,
	 * null the source) the macro's move-ctor did.
	 * ASAN-GATE: scaled mip-view chain + per-image default view lifetime; verify
	 * one decref per view (no leak / double-free). */
	struct ImageViewHandle { ImageView *data; };
	static inline struct ImageViewHandle iv_make(ImageView *p) { struct ImageViewHandle h; h.data = p; return h; }
	static inline void iv_reset(struct ImageViewHandle *h) { if (h->data) imageview_release_reference(h->data); h->data = NULL; }
	static inline ImageView *iv_get(const struct ImageViewHandle *h) { return h->data; }
	static inline int iv_is_valid(const struct ImageViewHandle *h) { return h->data != NULL; }
	static inline void iv_steal(struct ImageViewHandle *dst, struct ImageViewHandle *src) { dst->data = src->data; src->data = NULL; }

	/* Owning array of ImageViewHandle (= IntrusivePtr<ImageView>). Replaces
	 * std::vector<ImageViewHandle> for the renderer's scaled mip-view chain,
	 * which is built once (push during init) and then only indexed/read. The
	 * element is a refcounting handle with a non-trivial destructor, so growth
	 * move-constructs each handle into new storage with placement new and
	 * destroys the moved-from slot, and the destructor runs each handle's
	 * destructor (dropping its ref). push() takes ownership by move (no incref),
	 * matching push_back(device.create_image_view(...)) of a temporary. Move-
	 * only at the container level; indexed access returns a mutable reference so
	 * existing iv_get(&scaled_views[i]) / *iv_get(&scaled_views[i]) uses are unchanged. */
	struct ImageViewHandleVec {
		ImageViewHandle *items;
		int count;
		int cap;

		ImageViewHandleVec() : items(NULL), count(0), cap(0) {}
		~ImageViewHandleVec() { destroy(); }
		ImageViewHandleVec(ImageViewHandleVec &&o) noexcept
			: items(o.items), count(o.count), cap(o.cap) { o.items = NULL; o.count = 0; o.cap = 0; }
		ImageViewHandleVec &operator=(ImageViewHandleVec &&o) noexcept {
			if (this != &o) {
				destroy();
				items = o.items; count = o.count; cap = o.cap;
				o.items = NULL; o.count = 0; o.cap = 0;
			}
			return *this;
		}
		ImageViewHandleVec(const ImageViewHandleVec &) = delete;
		ImageViewHandleVec &operator=(const ImageViewHandleVec &) = delete;

		void push(ImageViewHandle &&v) {
			if (count >= cap)
				grow(cap ? cap * 2 : 8);
			iv_steal(&items[count], &v);
			count++;
		}
		int size() const { return count; }
		bool empty() const { return count == 0; }
		ImageViewHandle &operator[](int i) { return items[i]; }
		const ImageViewHandle &operator[](int i) const { return items[i]; }
		ImageViewHandle &front() { return items[0]; }

	private:
		void grow(int ncap) {
			ImageViewHandle *nitems = (ImageViewHandle *)malloc((size_t)ncap * sizeof(ImageViewHandle));
			for (int i = 0; i < count; i++) {
				/* Move: iv_steal copies the pointer and nulls the old slot,
				 * so no separate decref of the old slot is needed. */
				iv_steal(&nitems[i], &items[i]);
			}
			free(items);
			items = nitems;
			cap = ncap;
		}
		void destroy() {
			for (int i = 0; i < count; i++)
				iv_reset(&items[i]);
			free(items);
			items = NULL; count = 0; cap = 0;
		}
	};

	enum ImageDomain {
		ImageDomain_Physical,
		ImageDomain_Transient
	};

	struct ImageCreateInfo
	{
		ImageDomain domain = ImageDomain_Physical;
		unsigned width = 0;
		unsigned height = 0;
		unsigned depth = 1;
		unsigned levels = 1;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageType type = VK_IMAGE_TYPE_2D;
		unsigned layers = 1;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageCreateFlags flags = 0;
		ImageMiscFlags misc = 0;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		VkComponentMapping swizzle = {
			VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
		};

		static ImageCreateInfo immutable_2d_image(unsigned width, unsigned height, VkFormat format, bool mipmapped = false)
		{
			ImageCreateInfo info;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = mipmapped ? 0u : 1u;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = mipmapped ? unsigned(IMAGE_MISC_GENERATE_MIPS_BIT) : 0u;
			info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			return info;
		}

		static ImageCreateInfo render_target(unsigned width, unsigned height, VkFormat format)
		{
			ImageCreateInfo info;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = 1;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = 0;
			info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
			return info;
		}

		static ImageCreateInfo transient_render_target(unsigned width, unsigned height, VkFormat format)
		{
			ImageCreateInfo info;
			info.domain = ImageDomain_Transient;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = 1;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
				VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = 0;
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			return info;
		}
	};

	struct Image;

	struct ImageDeleter
	{
		void operator()(Image *image);
	};

	enum Layout {
		Layout_Optimal,
		Layout_General
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). The Cookie
	 * base, which provides the hash identity, is unrelated and stays. */
	/* Image: plain C struct + free functions (was a class deriving Cookie).
	 * Cookie embedded as cookie_base. Pooled; refcount starts at 1. Holds the
	 * default-view ImageViewHandle (already a plain struct). The deleted move
	 * ctor/assign of the old class simply do not exist for a plain struct. */
	struct Image
	{
		struct Cookie cookie_base;
		Device *device;
		VkImage image;
		ImageViewHandle view;
		DeviceAllocation alloc;
		ImageCreateInfo create_info;
		Layout layout_type;
		VkPipelineStageFlags stage_flags;
		VkAccessFlags access_flags;
		HandleCounter reference_count;
	};
	void image_init(struct Image *self, Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc, const ImageCreateInfo &info);
	void image_fini(struct Image *self);
	static inline const ImageView &image_get_view_const(const struct Image *self) { VK_ASSERT(iv_is_valid(&self->view)); return *iv_get(&self->view); }
	static inline ImageView &image_get_view(struct Image *self) { VK_ASSERT(iv_is_valid(&self->view)); return *iv_get(&self->view); }
	static inline VkImage image_get_image(const struct Image *self) { return self->image; }
	static inline VkFormat image_get_format(const struct Image *self) { return self->create_info.format; }
	static inline uint32_t image_get_width(const struct Image *self, uint32_t lod) { uint32_t v = self->create_info.width >> lod; return v > 1u ? v : 1u; }
	static inline uint32_t image_get_height(const struct Image *self, uint32_t lod) { uint32_t v = self->create_info.height >> lod; return v > 1u ? v : 1u; }
	static inline uint32_t image_get_depth(const struct Image *self, uint32_t lod) { uint32_t v = self->create_info.depth >> lod; return v > 1u ? v : 1u; }
	static inline const ImageCreateInfo &image_get_create_info(const struct Image *self) { return self->create_info; }
	static inline VkImageLayout image_get_layout(const struct Image *self, VkImageLayout optimal) { return self->layout_type == Layout_Optimal ? optimal : VK_IMAGE_LAYOUT_GENERAL; }
	static inline Layout image_get_layout_type(const struct Image *self) { return self->layout_type; }
	static inline void image_set_layout(struct Image *self, Layout layout) { self->layout_type = layout; }
	static inline void image_set_stage_flags(struct Image *self, VkPipelineStageFlags flags) { self->stage_flags = flags; }
	static inline void image_set_access_flags(struct Image *self, VkAccessFlags flags) { self->access_flags = flags; }
	static inline VkPipelineStageFlags image_get_stage_flags(const struct Image *self) { return self->stage_flags; }
	static inline VkAccessFlags image_get_access_flags(const struct Image *self) { return self->access_flags; }
	static inline const DeviceAllocation &image_get_allocation(const struct Image *self) { return self->alloc; }
	static inline void image_add_reference(struct Image *self) { counter_add_ref(&self->reference_count); }
	static void image_release_reference(struct Image *self);

	/* Image handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(ImageHandle, Image)
	 * to a plain struct + explicit free functions, following the
	 * sem_ / fence_ / smh_ / bvh_ / iv_ template. The pointee Image is unchanged.
	 * NOTE: the HD-texture caches (HdTexEntry.image, CachedGpuImage.image) store
	 * raw Image* with their own manual add_reference/release_reference and are
	 * NOT ImageHandle -- left untouched. Only ImageHandle value members and the
	 * ScanoutHandleVec container are converted. Accounting helpers:
	 *   ih_make   - wrap a freshly-produced Image* (refcount already 1)
	 *   ih_reset  - drop one reference, null the handle
	 *   ih_copy   - copy with incref (retain)
	 *   ih_assign - release-old / retain-new (member = otherHandle)
	 *   ih_move   - release-old / take producer ref (member = produced temp)
	 *   ih_steal  - move (copy ptr, null source); container grow
	 * ASAN-GATE: per-frame framebuffer set + scanout chain + transient/HD nodes;
	 * verify one decref per image (no leak / double-free). */
	struct ImageHandle { Image *data; };
	static inline struct ImageHandle ih_make(Image *p) { struct ImageHandle h; h.data = p; return h; }
	static inline void ih_reset(struct ImageHandle *h) { if (h->data) image_release_reference(h->data); h->data = NULL; }
	static inline Image *ih_get(const struct ImageHandle *h) { return h->data; }
	static inline int ih_is_valid(const struct ImageHandle *h) { return h->data != NULL; }
	static inline void ih_copy(struct ImageHandle *dst, const struct ImageHandle *src) {
		dst->data = src->data;
		if (dst->data) image_add_reference(dst->data);
	}
	static inline void ih_assign(struct ImageHandle *dst, const struct ImageHandle *src) {
		if (dst == src) return;
		if (src->data) image_add_reference(src->data);
		if (dst->data) image_release_reference(dst->data);
		dst->data = src->data;
	}
	/* Assign a freshly-produced handle (already holding one reference) into a
	 * member, releasing whatever the member held. */
	static inline void ih_move(struct ImageHandle *dst, struct ImageHandle produced) {
		if (dst->data) image_release_reference(dst->data);
		dst->data = produced.data;
	}
	static inline void ih_steal(struct ImageHandle *dst, struct ImageHandle *src) { dst->data = src->data; src->data = NULL; }

	class Device;

	struct FenceHolder;
	struct FenceHolderDeleter
	{
		void operator()(FenceHolder *fence);
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches release_reference/add_reference through
	 * the pointee directly). Mirrors the SemaphoreHolder conversion. */
	/* FenceHolder: plain C struct + free functions (was a class with refcount
	 * methods + ctor/dtor). Pooled via object_pool_raw; fenceholder_init starts
	 * the refcount at 1, fenceholder_fini runs the former destructor body, and
	 * the FenceHolderDeleter (invoked when the refcount hits 0) calls fini then
	 * returns the slot to the pool. */
	struct FenceHolder
	{
		Device *device;
		VkFence fence;
		HandleCounter reference_count;
	};
	static void fenceholder_init(struct FenceHolder *self, Device *device, VkFence fence);
	void fenceholder_fini(struct FenceHolder *self);
	static void fenceholder_wait(struct FenceHolder *self);
	static inline void fenceholder_add_reference(struct FenceHolder *self) { counter_add_ref(&self->reference_count); }
	static void fenceholder_release_reference(struct FenceHolder *self);

	/* Fence handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(Fence, FenceHolder)
	 * to a plain struct + explicit free functions, following the bvh_* template
	 * used for BufferViewHandle. The pointee FenceHolder keeps its
	 * add_reference/release_reference methods (the shared refcount interface is
	 * converted later, all pointees at once). Ownership accounting is now manual:
	 *   - fence_make() takes ownership of a freshly-constructed FenceHolder
	 *     (refcount starts at 1), no incref.
	 *   - fence_reset() drops one reference and nulls the handle.
	 *   - struct copy / return-by-value does NOT incref (matches the old
	 *     move-ctor steal); the source must be treated as moved-from and NOT
	 *     reset. The single surviving owner calls fence_reset() at scope exit.
	 * ASAN-GATE: every Fence scope-exit must map to exactly one fence_reset();
	 * the producer (submit_nolock) and the two consumers (flush_and_signal,
	 * read_buffer path) are the only lifetime sites. Verify no leak / no
	 * double-free on a fence-heavy workload (VRAM readback / FMV). */
	struct Fence { FenceHolder *data; };
	static inline struct Fence fence_make(FenceHolder *p) { struct Fence h; h.data = p; return h; }
	static inline void fence_reset(struct Fence *h) { if (h->data) fenceholder_release_reference(h->data); h->data = NULL; }
	static inline FenceHolder *fence_get(struct Fence *h) { return h->data; }
	static inline int fence_is_valid(const struct Fence *h) { return h->data != NULL; }

	POD_VEC_DECLARE(FenceVec, VkFence);
	/* FenceManager: recycles VkFence handles. Converted from a C++ class to a
	 * plain C struct + fencemanager_* free functions. init/init_empty stay inline;
	 * deinit/request_cleared_fence/recycle_fence are out-of-line. */
	struct FenceManager
	{
		VkDevice device;
		FenceVec fences;
	};
	static inline void fencemanager_init(struct FenceManager *self, VkDevice device) { self->device = device; }
	static inline void fencemanager_init_empty(struct FenceManager *self)
	{
		self->device = VK_NULL_HANDLE;
		self->fences.items = NULL;
		self->fences.count = 0;
		self->fences.cap   = 0;
	}
	static void fencemanager_deinit(struct FenceManager *self);
	static VkFence fencemanager_request_cleared_fence(struct FenceManager *self);
	static void fencemanager_recycle_fence(struct FenceManager *self, VkFence fence);

	class Device;

	struct SemaphoreHolder;
	struct SemaphoreHolderDeleter
	{
		void operator()(SemaphoreHolder *semaphore);
	};

	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr now dispatches release_reference/add_reference
	 * through the pointee directly). release_reference frees through the same
	 * deleter path on reaching zero. This is the first pointee taken off the
	 * template base, toward concrete per-type handles. */
	/* SemaphoreHolder: plain C struct + free functions (was a class with refcount
	 * methods + ctor/dtor). Pooled via object_pool_raw; the refcount starts at 1.
	 * semaphoreholder_consume hands out the VkSemaphore and clears the holder;
	 * the destructor (semaphoreholder_fini) either destroys or recycles the
	 * semaphore depending on whether it is still signalled. */
	struct SemaphoreHolder
	{
		Device *device;
		VkSemaphore semaphore;
		bool signalled;
		HandleCounter reference_count;
	};
	static void semaphoreholder_init(struct SemaphoreHolder *self, Device *device, VkSemaphore semaphore, bool signalled);
	void semaphoreholder_fini(struct SemaphoreHolder *self);
	static inline bool semaphoreholder_is_signalled(const struct SemaphoreHolder *self) { return self->signalled; }
	static inline VkSemaphore semaphoreholder_consume(struct SemaphoreHolder *self)
	{
		VkSemaphore ret = self->semaphore;
		VK_ASSERT(self->semaphore);
		VK_ASSERT(self->signalled);
		self->semaphore = VK_NULL_HANDLE;
		self->signalled = false;
		return ret;
	}
	static inline void semaphoreholder_add_reference(struct SemaphoreHolder *self) { counter_add_ref(&self->reference_count); }
	static void semaphoreholder_release_reference(struct SemaphoreHolder *self);

	/* Semaphore handle: de-RAII'd from INTRUSIVE_HANDLE_DECLARE(Semaphore,
	 * SemaphoreHolder) to a plain struct + explicit free functions, following the
	 * fence_ / bvh_ template. The pointee SemaphoreHolder keeps its
	 * add_reference/release_reference methods. sem_copy() performs the incref
	 * that the macro's copy-ctor did; sem_reset() the decref the dtor did; a bare
	 * struct copy with no incref is the move/steal. SemaphoreHandleVec below is
	 * updated to call these explicitly in place of placement-new-copy / ~dtor.
	 * ASAN-GATE: GPU wait-semaphore lifetime; verify no leak / double-free on a
	 * multi-queue (async-compute/transfer) workload. */
	struct Semaphore { SemaphoreHolder *data; };
	static inline struct Semaphore sem_make(SemaphoreHolder *p) { struct Semaphore h; h.data = p; return h; }
	static inline void sem_reset(struct Semaphore *h) { if (h->data) semaphoreholder_release_reference(h->data); h->data = NULL; }
	static inline SemaphoreHolder *sem_get(const struct Semaphore *h) { return h->data; }
	static inline int sem_is_valid(const struct Semaphore *h) { return h->data != NULL; }
	/* Copy-with-incref: assign src into dst, taking a new reference (matches the
	 * old copy-ctor used when a wait list is filled from a const Semaphore &). */
	static inline void sem_copy(struct Semaphore *dst, const struct Semaphore *src) {
		dst->data = src->data;
		if (dst->data) semaphoreholder_add_reference(dst->data);
	}

	/* Owning array of Semaphore (= IntrusivePtr<SemaphoreHolder>). Replaces
	 * std::vector<Semaphore> for the per-queue wait-semaphore lists. The element
	 * is a refcounting handle with a non-trivial destructor, so growth copy-
	 * constructs each handle into new storage with placement new and destroys
	 * the old slot, and clear()/the destructor run each handle's destructor
	 * (dropping its ref). push() copy-inserts (the wait list is filled from a
	 * const Semaphore &, an incref, as the std::vector was). clear() keeps the
	 * capacity so the lists are reused across frames. Distinct from SemaphoreVec
	 * above, which holds raw VkSemaphore handles (POD). */
	/* SemaphoreHandleVec: owning growable array of Semaphore handles (refcounting
	 * smart pointers; sem_copy retains, sem_reset releases). Converted from a
	 * move-only C++ container to a plain C struct + sem_handle_vec_* free
	 * functions; the embedding QueueData is zero-initialised (its empty state) and
	 * torn down explicitly. */
	struct SemaphoreHandleVec {
		Semaphore *items;
		int count;
		int cap;
	};

	static inline void sem_handle_vec_init(struct SemaphoreHandleVec *v)
	{
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	static inline void sem_handle_vec_grow(struct SemaphoreHandleVec *v, int ncap)
	{
		Semaphore *nitems = (Semaphore *)malloc((size_t)ncap * sizeof(Semaphore));
		int i;
		for (i = 0; i < v->count; i++) {
			/* Move the handle to new storage: plain struct copy with no refcount
			 * change (old copy-ctor incref + old-slot dtor decref cancelled out).
			 * The old slot is abandoned, not reset. */
			nitems[i] = v->items[i];
		}
		free(v->items);
		v->items = nitems;
		v->cap = ncap;
	}
	static inline void sem_handle_vec_push(struct SemaphoreHandleVec *v, const Semaphore *e)
	{
		if (v->count >= v->cap)
			sem_handle_vec_grow(v, v->cap ? v->cap * 2 : 8);
		sem_copy(&v->items[v->count], e);
		v->count++;
	}
	static inline int sem_handle_vec_size(const struct SemaphoreHandleVec *v) { return v->count; }
	static inline Semaphore *sem_handle_vec_at(struct SemaphoreHandleVec *v, int i) { return &v->items[i]; }
	static inline void sem_handle_vec_clear(struct SemaphoreHandleVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++)
			sem_reset(&v->items[i]);
		v->count = 0;
	}
	static inline void sem_handle_vec_deinit(struct SemaphoreHandleVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++)
			sem_reset(&v->items[i]);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}

	POD_VEC_DECLARE(SemaphoreVec, VkSemaphore);
	/* SemaphoreManager: recycles VkSemaphore handles. Converted from a C++ class
	 * to a plain C struct + semaphoremanager_* free functions. */
	struct SemaphoreManager
	{
		VkDevice device;
		SemaphoreVec semaphores;
	};
	static inline void semaphoremanager_init(struct SemaphoreManager *self, VkDevice device) { self->device = device; }
	static inline void semaphoremanager_init_empty(struct SemaphoreManager *self)
	{
		self->device = VK_NULL_HANDLE;
		self->semaphores.items = NULL;
		self->semaphores.count = 0;
		self->semaphores.cap   = 0;
	}
	static void semaphoremanager_deinit(struct SemaphoreManager *self);
	static VkSemaphore semaphoremanager_request_cleared_semaphore(struct SemaphoreManager *self);
	static void semaphoremanager_recycle(struct SemaphoreManager *self, VkSemaphore semaphore);

	class Device;
	struct DescriptorSetLayout
	{
		uint32_t sampled_image_mask = 0;
		uint32_t storage_image_mask = 0;
		uint32_t uniform_buffer_mask = 0;
		uint32_t storage_buffer_mask = 0;
		uint32_t sampled_buffer_mask = 0;
		uint32_t input_attachment_mask = 0;
		uint32_t sampler_mask = 0;
		uint32_t separate_image_mask = 0;
		uint32_t fp_mask = 0;
		uint32_t immutable_sampler_mask = 0;
		uint64_t immutable_samplers = 0;
	};

	// Avoid -Wclass-memaccess warnings since we hash DescriptorSetLayout.

	static inline bool has_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
	{
		return (layout.immutable_sampler_mask & (1u << binding)) != 0;
	}

	static inline StockSampler get_immutable_sampler(const DescriptorSetLayout &layout, unsigned binding)
	{
		VK_ASSERT(has_immutable_sampler(layout, binding));
		return (StockSampler)((layout.immutable_samplers >> (4 * binding)) & 0xf);
	}

	static inline void set_immutable_sampler(DescriptorSetLayout &layout, unsigned binding, StockSampler sampler)
	{
		layout.immutable_samplers |= uint64_t(sampler) << (4 * binding);
		layout.immutable_sampler_mask |= 1u << binding;
	}

	static const unsigned VULKAN_NUM_SETS_PER_POOL = 16;
	static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 8;

	POD_VEC_DECLARE(DescriptorPoolVec, VkDescriptorPool);
	POD_VEC_DECLARE(DescriptorPoolSizeVec, VkDescriptorPoolSize);
	POD_VEC_DECLARE(DescriptorBindingVec, VkDescriptorSetLayoutBinding);

	/* Result of DescriptorSetAllocator::find: the set plus whether it was an
	 * existing (already-populated) entry. Replaces std::pair<VkDescriptorSet,
	 * bool>; 'cached' is the old .second. */
	struct DescriptorSetAllocation
	{
		VkDescriptorSet set;
		bool cached;
	};

	/* Lifted to file scope (was nested in DescriptorSetAllocator) so the concrete
	 * temp-map's free functions can be stamped at namespace scope. */
	struct DescriptorSetNode
	{
		struct TemporaryHashmapNode th_node;
		DescriptorSetNode(VkDescriptorSet set)
			: set(set)
		{
		}

		VkDescriptorSet set;
	};

	static inline void descriptor_set_node_destroy(DescriptorSetNode *t) { t->~DescriptorSetNode(); }
	VK_TEMPHASH_DECLARE(descriptor_set_thmap, DescriptorSetNode, VULKAN_DESCRIPTOR_RING_SIZE, 1, descriptor_set_node_destroy)

	/* make_vacant constructs a node in the pool and parks it on the vacant free-list
	 * (the reuse path); mirrors the template's variadic make_vacant for this type. */
	static inline void descriptor_set_thmap_make_vacant(struct descriptor_set_thmap *m, VkDescriptorSet set)
	{
		void *slot = object_pool_raw_allocate(&m->object_pool);
		DescriptorSetNode *node;
		if (!slot)
			return;
		node = new (slot) DescriptorSetNode(set);
		descriptor_set_thmap_vacant_push(m, node);
	}

	/* DescriptorSetAllocator: cached VkDescriptorSetLayout + per-thread descriptor
	 * pool/set recycling. Lives in an IntrusiveHashMap (intrusive_node first).
	 * Converted from a C++ class to a plain C struct + descriptor_set_allocator_*
	 * free functions. The nested PerThread struct is hoisted to file scope as
	 * DescriptorSetAllocatorPerThread; ctor -> _init (called by the map emplace),
	 * dtor -> _fini (the map destroy callback). */
	struct DescriptorSetAllocatorPerThread
	{
		struct descriptor_set_thmap set_nodes;
		DescriptorPoolVec pools;
		bool should_begin;
	};

	struct DescriptorSetAllocator
	{
		struct IntrusiveHashMapNode intrusive_node; /* must stay first (offset 0) */
		Device *device;
		VkDescriptorSetLayout set_layout;
		struct DescriptorSetAllocatorPerThread per_thread;
		DescriptorPoolSizeVec pool_size;
	};

	void descriptor_set_allocator_init(struct DescriptorSetAllocator *self, Hash hash, Device *device, const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings);
	void descriptor_set_allocator_fini(struct DescriptorSetAllocator *self);
	DescriptorSetAllocation descriptor_set_allocator_find(struct DescriptorSetAllocator *self, Hash hash);
	void descriptor_set_allocator_clear(struct DescriptorSetAllocator *self);
	static inline void descriptor_set_allocator_begin_frame(struct DescriptorSetAllocator *self) { self->per_thread.should_begin = true; }
	static inline VkDescriptorSetLayout descriptor_set_allocator_get_layout(const struct DescriptorSetAllocator *self) { return self->set_layout; }

/* ============================================================
 * shader.hpp
 * ============================================================ */

	class Device;

	enum ShaderStage {
		ShaderStage_Vertex = 0,
		ShaderStage_TessControl = 1,
		ShaderStage_TessEvaluation = 2,
		ShaderStage_Geometry = 3,
		ShaderStage_Fragment = 4,
		ShaderStage_Compute = 5,
		ShaderStage_Count
	};

	struct ResourceLayout
	{
		uint32_t input_mask = 0;
		uint32_t output_mask = 0;
		uint32_t push_constant_size = 0;
		uint32_t spec_constant_mask = 0;
		DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS];
	};

	struct CombinedResourceLayout
	{
		uint32_t attribute_mask = 0;
		uint32_t render_target_mask = 0;
		DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		uint32_t stages_for_bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
		uint32_t stages_for_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		VkPushConstantRange push_constant_range = {};
		uint32_t descriptor_set_mask = 0;
		uint32_t spec_constant_mask[(unsigned)ShaderStage_Count] = {};
		uint32_t combined_spec_constant_mask = 0;
		Hash push_constant_layout_hash = 0;
	};

	/* PipelineLayout: cached VkPipelineLayout + its resource layout and per-set
	 * allocators. Lives in an IntrusiveHashMap (intrusive_node first). Converted
	 * from a C++ class to a plain C struct + pipeline_layout_* free functions; the
	 * ctor becomes pipeline_layout_init (called by the map's emplace after the slot
	 * is allocated) and the dtor becomes pipeline_layout_fini (the map's destroy
	 * callback). */
	struct PipelineLayout
	{
		struct IntrusiveHashMapNode intrusive_node; /* must stay first (offset 0) */
		Device *device;
		VkPipelineLayout pipe_layout;
		CombinedResourceLayout layout;
		DescriptorSetAllocator *set_allocators[VULKAN_NUM_DESCRIPTOR_SETS];
	};

	void pipeline_layout_init(struct PipelineLayout *self, Hash hash, Device *device, const CombinedResourceLayout &layout);
	void pipeline_layout_fini(struct PipelineLayout *self);
	static inline const CombinedResourceLayout *pipeline_layout_get_resource_layout(const struct PipelineLayout *self) { return &self->layout; }
	static inline VkPipelineLayout pipeline_layout_get_layout(const struct PipelineLayout *self) { return self->pipe_layout; }
	static inline DescriptorSetAllocator *pipeline_layout_get_allocator(const struct PipelineLayout *self, unsigned set) { return self->set_allocators[set]; }

	/* Shader: cached VkShaderModule + its reflected ResourceLayout. Lives in an
	 * IntrusiveHashMap (intrusive_node first). Converted from a C++ class to a
	 * plain C struct + shader_* free functions; ctor -> shader_init (called by the
	 * map's emplace), dtor -> shader_fini (the map's destroy callback). */
	struct Shader
	{
		struct IntrusiveHashMapNode intrusive_node; /* must stay first (offset 0) */
		Device *device;
		VkShaderModule module;
		ResourceLayout layout;
	};

	void shader_init(struct Shader *self, Hash hash, Device *device, const uint32_t *data, size_t size);
	void shader_fini(struct Shader *self);
	static inline const ResourceLayout *shader_get_layout(const struct Shader *self) { return &self->layout; }
	static inline VkShaderModule shader_get_module(const struct Shader *self) { return self->module; }
	const char *shader_stage_to_name(ShaderStage stage);

	/* Program: cached shader set + pipeline layout + per-program VkPipeline cache.
	 * Lives in an IntrusiveHashMap (intrusive_node first). Converted from a C++
	 * class to a plain C struct + program_* free functions. The two ctors
	 * (graphics: vertex+fragment / compute) become program_init_graphics /
	 * program_init_compute (called by the two map emplace variants); the dtor
	 * becomes program_fini (the map's destroy callback). */
	struct Program
	{
		struct IntrusiveHashMapNode intrusive_node; /* must stay first (offset 0) */
		Device *device;
		Shader *shaders[(unsigned)ShaderStage_Count];
		PipelineLayout *layout;
		struct vk_pipeline_map pipelines;
	};

	static inline void program_set_shader(struct Program *self, ShaderStage stage, Shader *handle)
	{
		self->shaders[(unsigned)stage] = handle;
	}
	void program_init_graphics(struct Program *self, Device *device, Shader *vertex, Shader *fragment);
	void program_init_compute(struct Program *self, Device *device, Shader *compute);
	void program_fini(struct Program *self);
	static inline const Shader *program_get_shader(const struct Program *self, ShaderStage stage) { return self->shaders[(unsigned)stage]; }
	static inline void program_set_pipeline_layout(struct Program *self, PipelineLayout *new_layout) { self->layout = new_layout; }
	static inline PipelineLayout *program_get_pipeline_layout(const struct Program *self) { return self->layout; }
	VkPipeline program_get_pipeline(const struct Program *self, Hash hash);
	VkPipeline program_add_pipeline(struct Program *self, Hash hash, VkPipeline pipeline);

/* ============================================================
 * render_pass.hpp
 * ============================================================ */

	enum RenderPassOp
	{
		RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 0,
		RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 1,
		RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 2,
		RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT = 1 << 3,
		RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT = 1 << 4,
		RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT = 1 << 5
	};
	using RenderPassOpFlags = uint32_t;

	struct ImageView;
	struct RenderPassInfo
	{
		ImageView *color_attachments[VULKAN_NUM_ATTACHMENTS];
		ImageView *depth_stencil = NULL;
		unsigned num_color_attachments = 0;
		RenderPassOpFlags op_flags = 0;
		uint32_t clear_attachments = 0;
		uint32_t load_attachments = 0;
		uint32_t store_attachments = 0;
		uint32_t layer = 0;

		// Render area will be clipped to the actual framebuffer.
		VkRect2D render_area = { { 0, 0 }, { UINT32_MAX, UINT32_MAX } };

		VkClearColorValue clear_color[VULKAN_NUM_ATTACHMENTS] = {};
		VkClearDepthStencilValue clear_depth_stencil = { 1.0f, 0 };

		enum DepthStencil {
			DepthStencil_None,
			DepthStencil_ReadOnly,
			DepthStencil_ReadWrite
		};

		struct Subpass
		{
			uint32_t color_attachments[VULKAN_NUM_ATTACHMENTS];
			uint32_t input_attachments[VULKAN_NUM_ATTACHMENTS];
			uint32_t resolve_attachments[VULKAN_NUM_ATTACHMENTS];
			unsigned num_color_attachments = 0;
			unsigned num_input_attachments = 0;
			unsigned num_resolve_attachments = 0;
			DepthStencil depth_stencil_mode = DepthStencil_ReadWrite;
		};
		// If 0/nullptr, assume a default subpass.
		const Subpass *subpasses = NULL;
		unsigned num_subpasses = 0;
	};

	/* RenderPass: cached VkRenderPass + per-subpass attachment metadata. Lives in
	 * an IntrusiveHashMap (intrusive_node first). Converted from a C++ class to a
	 * plain C struct + render_pass_* free functions; ctor -> render_pass_init
	 * (called by the map's emplace), dtor -> render_pass_fini (the map's destroy
	 * callback). The nested SubpassInfo type and its POD vector stay as-is. */
	struct RenderPassSubpassInfo
	{
		VkAttachmentReference color_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_color_attachments;
		VkAttachmentReference input_attachments[VULKAN_NUM_ATTACHMENTS];
		unsigned num_input_attachments;
		VkAttachmentReference depth_stencil_attachment;

		unsigned samples;
	};
	POD_VEC_DECLARE(SubpassInfoVec, RenderPassSubpassInfo);

	struct RenderPass
	{
		struct IntrusiveHashMapNode intrusive_node; /* must stay first (offset 0) */
		Device *device;
		VkRenderPass render_pass;
		VkFormat color_attachments[VULKAN_NUM_ATTACHMENTS];
		VkFormat depth_stencil;
		SubpassInfoVec subpasses;
	};

	static void render_pass_fixup_render_pass_nvidia(struct RenderPass *self, VkRenderPassCreateInfo &create_info, VkAttachmentDescription *attachments);
	void render_pass_init(struct RenderPass *self, Hash hash, Device *device, const RenderPassInfo &info);
	void render_pass_fini(struct RenderPass *self);

	static inline VkRenderPass render_pass_get_render_pass(const struct RenderPass *self) { return self->render_pass; }
	static inline uint32_t render_pass_get_sample_count(const struct RenderPass *self, unsigned subpass)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		return SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->samples;
	}
	static inline unsigned render_pass_get_num_color_attachments(const struct RenderPass *self, unsigned subpass)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		return SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->num_color_attachments;
	}
	static inline unsigned render_pass_get_num_input_attachments(const struct RenderPass *self, unsigned subpass)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		return SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->num_input_attachments;
	}
	static inline const VkAttachmentReference *render_pass_get_color_attachment(const struct RenderPass *self, unsigned subpass, unsigned index)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		VK_ASSERT(index < SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->num_color_attachments);
		return &SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->color_attachments[index];
	}
	static inline const VkAttachmentReference *render_pass_get_input_attachment(const struct RenderPass *self, unsigned subpass, unsigned index)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		VK_ASSERT(index < SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->num_input_attachments);
		return &SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->input_attachments[index];
	}
	static inline bool render_pass_has_depth(const struct RenderPass *self, unsigned subpass)
	{
		VK_ASSERT(subpass < SubpassInfoVec_size(&self->subpasses));
		return SubpassInfoVec_at((struct SubpassInfoVec *)&self->subpasses, subpass)->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
			format_has_depth_aspect(self->depth_stencil);
	}

	/* The concrete hash map recovers each node type from an IntrusiveHashMapNode* by
	 * casting, which is only valid if the node base is at offset 0 of every node. */
	static_assert(offsetof(DescriptorSetAllocator, intrusive_node) == 0,
			"DescriptorSetAllocator.intrusive_node must be first");
	static_assert(offsetof(PipelineLayout, intrusive_node) == 0,
			"PipelineLayout.intrusive_node must be first");
	static_assert(offsetof(Shader, intrusive_node) == 0,
			"Shader.intrusive_node must be first");
	static_assert(offsetof(Program, intrusive_node) == 0,
			"Program.intrusive_node must be first");
	static_assert(offsetof(RenderPass, intrusive_node) == 0,
			"RenderPass.intrusive_node must be first");

	/* Concrete per-type maps for the five Device-level cache node types. Each
	 * destroy hook runs the type's (non-trivial) destructor before the slot returns
	 * to the pool; the dtor call keeps this translation unit C++ for now (a later
	 * milestone switches the file to a C compiler, at which point these become
	 * explicit teardown calls). The per-type emplace_yield constructs the node in
	 * the pool with the type's real constructor, then yields it into the holder. The
	 * map key (hash) is passed to the constructor too for the four types whose
	 * constructor records it; Program's constructor does not take a hash, so there
	 * the hash is used only as the map key. */
	static inline void pipeline_layout_map_destroy(PipelineLayout *t) { pipeline_layout_fini(t); }
	static inline void descriptor_set_allocator_map_destroy(DescriptorSetAllocator *t) { descriptor_set_allocator_fini(t); }
	static inline void render_pass_map_destroy(RenderPass *t) { render_pass_fini(t); }
	static inline void shader_map_destroy(Shader *t) { shader_fini(t); }
	static inline void program_map_destroy(Program *t) { program_fini(t); }

	VK_HASHMAP_DECLARE(pipeline_layout_map, PipelineLayout, pipeline_layout_map_destroy)
	VK_HASHMAP_DECLARE(descriptor_set_allocator_map, DescriptorSetAllocator, descriptor_set_allocator_map_destroy)
	VK_HASHMAP_DECLARE(render_pass_map, RenderPass, render_pass_map_destroy)
	VK_HASHMAP_DECLARE(shader_map, Shader, shader_map_destroy)
	VK_HASHMAP_DECLARE(program_map, Program, program_map_destroy)

	static inline PipelineLayout *pipeline_layout_map_emplace_yield(struct pipeline_layout_map *m,
			Hash hash, Device *device, const CombinedResourceLayout &layout)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		PipelineLayout *t;
		if (!slot)
			return NULL;
		t = (PipelineLayout *)slot;
		pipeline_layout_init(t, hash, device, layout);
		return pipeline_layout_map_insert_yield(m, hash, t);
	}

	static inline DescriptorSetAllocator *descriptor_set_allocator_map_emplace_yield(
			struct descriptor_set_allocator_map *m, Hash hash, Device *device,
			const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		DescriptorSetAllocator *t;
		if (!slot)
			return NULL;
		t = (DescriptorSetAllocator *)slot;
		descriptor_set_allocator_init(t, hash, device, layout, stages_for_bindings);
		return descriptor_set_allocator_map_insert_yield(m, hash, t);
	}

	static inline RenderPass *render_pass_map_emplace_yield(struct render_pass_map *m,
			Hash hash, Device *device, const RenderPassInfo &info)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		RenderPass *t;
		if (!slot)
			return NULL;
		t = (RenderPass *)slot;
		render_pass_init(t, hash, device, info);
		return render_pass_map_insert_yield(m, hash, t);
	}

	static inline Shader *shader_map_emplace_yield(struct shader_map *m,
			Hash hash, Device *device, const uint32_t *data, size_t size)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		Shader *t;
		if (!slot)
			return NULL;
		t = (Shader *)slot;
		shader_init(t, hash, device, data, size);
		return shader_map_insert_yield(m, hash, t);
	}

	/* Program has two constructors (graphics vertex+fragment, and compute); one
	 * emplace helper per arity. Neither constructor takes the hash. */
	static inline Program *program_map_emplace_yield_compute(struct program_map *m,
			Hash hash, Device *device, Shader *compute)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		Program *t;
		if (!slot)
			return NULL;
		t = (Program *)slot;
		program_init_compute(t, device, compute);
		return program_map_insert_yield(m, hash, t);
	}

	static inline Program *program_map_emplace_yield_graphics(struct program_map *m,
			Hash hash, Device *device, Shader *vertex, Shader *fragment)
	{
		void *slot = object_pool_raw_allocate(&m->pool);
		Program *t;
		if (!slot)
			return NULL;
		t = (Program *)slot;
		program_init_graphics(t, device, vertex, fragment);
		return program_map_insert_yield(m, hash, t);
	}

	/* Framebuffer: cached VkFramebuffer + its attachments. Converted from a C++
	 * class to a plain C struct + framebuffer_* free functions. The Cookie base is
	 * embedded as the first member (composition, seeded by cookie_init); the former
	 * const RenderPass& reference member becomes a pointer (plain structs cannot
	 * hold references). FramebufferNode embeds a Framebuffer by value as its base.
	 * ctor -> framebuffer_init, dtor -> framebuffer_fini. */
	struct Framebuffer
	{
		struct Cookie cookie_base; /* was: public Cookie base subobject */
		Device *device;
		VkFramebuffer framebuffer;
		const RenderPass *render_pass;
		RenderPassInfo info;
		uint32_t width;
		uint32_t height;
		ImageView *attachments[VULKAN_NUM_ATTACHMENTS + 1];
		unsigned num_attachments;
	};

	void framebuffer_init(struct Framebuffer *self, Device *device, const RenderPass *rp, const RenderPassInfo &info);
	void framebuffer_fini(struct Framebuffer *self);
	static inline VkFramebuffer framebuffer_get_framebuffer(const struct Framebuffer *self) { return self->framebuffer; }
	static inline ImageView *framebuffer_get_attachment(const struct Framebuffer *self, unsigned index)
	{
		assert(index < self->num_attachments);
		return self->attachments[index];
	}
	static inline uint32_t framebuffer_get_width(const struct Framebuffer *self) { return self->width; }
	static inline uint32_t framebuffer_get_height(const struct Framebuffer *self) { return self->height; }
	static inline const RenderPass *framebuffer_get_compatible_render_pass(const struct Framebuffer *self) { return self->render_pass; }

	static const unsigned VULKAN_FRAMEBUFFER_RING_SIZE = 8;
	/* Lifted to file scope (was nested in FramebufferAllocator). Derives from
	 * Framebuffer, so th_node is NOT at offset 0 - the ring lists recover the node
	 * with TH_NODE_OF, which the temp-map macro already uses. */
	struct FramebufferNode
	{
		struct Framebuffer base; /* was: inherit Framebuffer */
		struct TemporaryHashmapNode th_node;
	};

	static inline void framebuffer_node_destroy(FramebufferNode *t) { framebuffer_fini(&t->base); }
	VK_TEMPHASH_DECLARE(framebuffer_thmap, FramebufferNode, VULKAN_FRAMEBUFFER_RING_SIZE, 0, framebuffer_node_destroy)

	/* emplace constructs the node in the pool, stamps its ring index + hash, registers
	 * it in the recycle map and links it at the front of the current ring (mirrors the
	 * template's variadic emplace for this type). */
	static inline FramebufferNode *framebuffer_thmap_emplace(struct framebuffer_thmap *m,
			Hash hash, Device *device, const RenderPass *rp, const RenderPassInfo &info)
	{
		void *slot = object_pool_raw_allocate(&m->object_pool);
		FramebufferNode *node;
		if (!slot)
			return NULL;
		node = (FramebufferNode *)slot;
		framebuffer_init(&node->base, device, rp, info);
		node->th_node.index = m->index;
		node->th_node.hash  = hash;
		vk_ptr_map_emplace_replace(&m->hashmap, hash, (void *)node);
		ilist_insert_front(&m->rings[m->index], &node->th_node.list_node);
		return node;
	}

	/* FramebufferAllocator: per-frame framebuffer cache (a ring TempHashmap of
	 * FramebufferNodes). Converted from a C++ class to a plain C struct +
	 * framebuffer_allocator_* free functions. */
	struct FramebufferAllocator
	{
		Device *device;
		struct framebuffer_thmap framebuffers;
	};
	static inline void framebuffer_allocator_init(struct FramebufferAllocator *self, Device *device_)
	{
		self->device = device_;
		framebuffer_thmap_init_empty(&self->framebuffers);
	}
	static inline void framebuffer_allocator_deinit(struct FramebufferAllocator *self)
	{
		framebuffer_thmap_deinit(&self->framebuffers);
	}
	struct Framebuffer *framebuffer_allocator_request_framebuffer(struct FramebufferAllocator *self, const RenderPassInfo &info);
	void framebuffer_allocator_begin_frame(struct FramebufferAllocator *self);
	void framebuffer_allocator_clear(struct FramebufferAllocator *self);

	/* Lifted to file scope (was nested in AttachmentAllocator). */
	struct TransientNode
	{
		struct TemporaryHashmapNode th_node;
		TransientNode(ImageHandle handle_)
		{
			/* Plain struct copy: the produced handle (refcount 1) is passed
			 * by value through emplace and moved into the member without an
			 * incref; the node becomes the single owner. */
			handle = handle_;
		}

		ImageHandle handle;
	};

	static inline void transient_node_destroy(TransientNode *t) { ih_reset(&t->handle); t->~TransientNode(); }
	VK_TEMPHASH_DECLARE(transient_thmap, TransientNode, VULKAN_FRAMEBUFFER_RING_SIZE, 0, transient_node_destroy)

	static inline TransientNode *transient_thmap_emplace(struct transient_thmap *m,
			Hash hash, ImageHandle handle)
	{
		void *slot = object_pool_raw_allocate(&m->object_pool);
		TransientNode *node;
		if (!slot)
			return NULL;
		node = new (slot) TransientNode(handle);
		node->th_node.index = m->index;
		node->th_node.hash  = hash;
		vk_ptr_map_emplace_replace(&m->hashmap, hash, (void *)node);
		ilist_insert_front(&m->rings[m->index], &node->th_node.list_node);
		return node;
	}

	/* AttachmentAllocator: per-frame transient-attachment cache (a ring TempHashmap
	 * of transient ImageView nodes). Converted from a C++ class to a plain C struct
	 * + attachment_allocator_* free functions. */
	struct AttachmentAllocator
	{
		Device *device;
		struct transient_thmap attachments;
	};
	static inline void attachment_allocator_init(struct AttachmentAllocator *self, Device *device_)
	{
		self->device = device_;
		transient_thmap_init_empty(&self->attachments);
	}
	static inline void attachment_allocator_deinit(struct AttachmentAllocator *self)
	{
		transient_thmap_deinit(&self->attachments);
	}
	struct ImageView *attachment_allocator_request_attachment(struct AttachmentAllocator *self, unsigned width, unsigned height, VkFormat format,
			unsigned index, unsigned samples, unsigned layers);
	void attachment_allocator_begin_frame(struct AttachmentAllocator *self);
	void attachment_allocator_clear(struct AttachmentAllocator *self);


/* ============================================================
 * buffer_pool.hpp
 * ============================================================ */

	class Device;
	struct Buffer;

	struct BufferBlockAllocation
	{
		uint8_t *host;
		VkDeviceSize offset;
	};

	/* BufferBlock: a sub-allocatable mapped buffer pair (gpu/cpu). Converted from
	 * a C++ struct (user copy ctor/assign/dtor managing the two refcounted handle
	 * members) to a plain C struct + bufferblock_* free functions. Because the gpu
	 * and cpu handles carry Buffer references, every former implicit copy/assign/
	 * move/destroy is now an explicit call:
	 *   bufferblock_init     - zero a fresh block
	 *   bufferblock_fini     - release both handle refs (was ~BufferBlock)
	 *   bufferblock_copy     - retain (was the copy ctor)
	 *   bufferblock_assign   - release-old / retain-new (was operator=)
	 *   bufferblock_steal    - move (copy fields, null the source; was the && move)
	 *   bufferblock_allocate - the sub-allocation bump (unchanged logic). */
	struct BufferBlock
	{
		struct BufferHandle gpu;
		struct BufferHandle cpu;
		VkDeviceSize offset;
		VkDeviceSize alignment;
		VkDeviceSize size;
		uint8_t *mapped;
	};

	static inline void bufferblock_init(struct BufferBlock *self)
	{
		self->gpu.data = NULL;
		self->cpu.data = NULL;
		self->offset = 0;
		self->alignment = 0;
		self->size = 0;
		self->mapped = NULL;
	}
	static inline void bufferblock_fini(struct BufferBlock *self)
	{
		bh_reset(&self->gpu);
		bh_reset(&self->cpu);
	}
	/* Retain-copy: dst takes its own references to o's two handles. */
	static inline void bufferblock_copy(struct BufferBlock *dst, const struct BufferBlock *o)
	{
		dst->gpu.data = o->gpu.data; if (dst->gpu.data) buffer_add_reference(dst->gpu.data);
		dst->cpu.data = o->cpu.data; if (dst->cpu.data) buffer_add_reference(dst->cpu.data);
		dst->offset = o->offset; dst->alignment = o->alignment; dst->size = o->size; dst->mapped = o->mapped;
	}
	static inline void bufferblock_assign(struct BufferBlock *dst, const struct BufferBlock *o)
	{
		if (dst != o)
		{
			if (o->gpu.data) buffer_add_reference(o->gpu.data);
			if (o->cpu.data) buffer_add_reference(o->cpu.data);
			if (dst->gpu.data) buffer_release_reference(dst->gpu.data);
			if (dst->cpu.data) buffer_release_reference(dst->cpu.data);
			dst->gpu.data = o->gpu.data; dst->cpu.data = o->cpu.data;
			dst->offset = o->offset; dst->alignment = o->alignment; dst->size = o->size; dst->mapped = o->mapped;
		}
	}
	/* Move: dst steals src's handles (no refcount change), src left empty. */
	static inline void bufferblock_steal(struct BufferBlock *dst, struct BufferBlock *src)
	{
		dst->gpu.data = src->gpu.data; dst->cpu.data = src->cpu.data;
		dst->offset = src->offset; dst->alignment = src->alignment; dst->size = src->size; dst->mapped = src->mapped;
		src->gpu.data = NULL; src->cpu.data = NULL;
	}

	static inline BufferBlockAllocation bufferblock_allocate(struct BufferBlock *self, VkDeviceSize allocate_size)
	{
		VkDeviceSize aligned_offset = (self->offset + self->alignment - 1) & ~(self->alignment - 1);
		if (aligned_offset + allocate_size <= self->size)
		{
			BufferBlockAllocation r;
			r.host = self->mapped + aligned_offset;
			r.offset = aligned_offset;
			self->offset = aligned_offset + allocate_size;
			return r;
		}
		else
		{
			BufferBlockAllocation r;
			r.host = NULL;
			r.offset = 0;
			return r;
		}
	}

	/* Owning array of BufferBlock for the buffer pool's recycle list. Plain C
	 * struct + bufferblock_vec_* free functions (was a move-only C++ container).
	 * push copy-retains via bufferblock_copy; clear/pop_back/free_storage run
	 * bufferblock_fini; grow steals each element into new storage. */
	struct BufferBlockVec {
		struct BufferBlock *items;
		int count;
		int cap;
	};
	static inline void bufferblock_vec_init(struct BufferBlockVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
	static inline void bufferblock_vec_grow(struct BufferBlockVec *v, int ncap) {
		struct BufferBlock *nitems = (struct BufferBlock *)malloc((size_t)ncap * sizeof(struct BufferBlock));
		int i;
		for (i = 0; i < v->count; i++)
			bufferblock_steal(&nitems[i], &v->items[i]);
		free(v->items);
		v->items = nitems;
		v->cap = ncap;
	}
	/* Copy-insert (retain), matching the old push_back of a copy. */
	static inline void bufferblock_vec_push(struct BufferBlockVec *v, const struct BufferBlock *b) {
		if (v->count >= v->cap)
			bufferblock_vec_grow(v, v->cap ? v->cap * 2 : 8);
		bufferblock_copy(&v->items[v->count], b);
		v->count++;
	}
	static inline int  bufferblock_vec_empty(const struct BufferBlockVec *v) { return v->count == 0; }
	static inline void bufferblock_vec_clear(struct BufferBlockVec *v) {
		int i;
		for (i = 0; i < v->count; i++)
			bufferblock_fini(&v->items[i]);
		v->count = 0;
	}
	static inline struct BufferBlock *bufferblock_vec_back(struct BufferBlockVec *v) { return &v->items[v->count - 1]; }
	static inline void bufferblock_vec_pop_back(struct BufferBlockVec *v) { if (v->count) { v->count--; bufferblock_fini(&v->items[v->count]); } }
	static inline void bufferblock_vec_free_storage(struct BufferBlockVec *v) {
		int i;
		for (i = 0; i < v->count; i++)
			bufferblock_fini(&v->items[i]);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}

	/* BufferPool: pools sub-allocatable mapped BufferBlocks. Converted from a C++
	 * class to a plain C struct + bufferpool_* free functions. request_block
	 * returns a BufferBlock by value (the caller owns the returned refs);
	 * recycle_block takes the block by pointer (was BufferBlock&&) and copy-
	 * retains it into the recycle list, so the caller must still bufferblock_fini
	 * its now-moved-from local. */
	struct BufferPool
	{
		Device *device;
		VkDeviceSize block_size;
		VkDeviceSize alignment;
		VkBufferUsageFlags usage;
		struct BufferBlockVec blocks;
	};
	static inline void bufferpool_init(struct BufferPool *self, Device *device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage)
	{
		self->device = device;
		self->block_size = block_size;
		self->alignment = alignment;
		self->usage = usage;
	}
	/* Raw-memory empty state, for a malloc'd owner. */
	static inline void bufferpool_init_empty(struct BufferPool *self)
	{
		self->device      = NULL;
		self->block_size  = 0;
		self->alignment   = 0;
		self->usage       = 0;
		self->blocks.items = NULL;
		self->blocks.count = 0;
		self->blocks.cap   = 0;
	}
	static inline void bufferpool_deinit(struct BufferPool *self)
	{
		VK_ASSERT(bufferblock_vec_empty(&self->blocks));
		bufferblock_vec_free_storage(&self->blocks);
	}
	static inline VkDeviceSize bufferpool_get_block_size(const struct BufferPool *self) { return self->block_size; }
	static void bufferpool_reset(struct BufferPool *self);
	static struct BufferBlock bufferpool_request_block(struct BufferPool *self, VkDeviceSize minimum_size);
	static void bufferpool_recycle_block(struct BufferPool *self, struct BufferBlock *block);

/* ============================================================
 * command_pool.hpp
 * ============================================================ */

	POD_VEC_DECLARE(CommandBufferVec, VkCommandBuffer);
	/* Per-queue transient command pool. Formerly a class with a constructor,
	 * destructor and (never-used) move ctor/assignment; now a plain struct driven
	 * by command_pool_init / command_pool_deinit. PerFrame embeds three of these
	 * by value and never moves them, so the move machinery was dead and is gone.
	 * signal_submitted stays inline (VULKAN_DEBUG-only bookkeeping). */
	struct CommandPool
	{
		VkDevice device;
		VkCommandPool pool;
		CommandBufferVec buffers;
#ifdef VULKAN_DEBUG
		CommandBufferVec in_flight;
#endif
		unsigned index;
	};

	static void command_pool_init(CommandPool *cp, VkDevice device, uint32_t queue_family_index);
	static void command_pool_deinit(CommandPool *cp);
	static void command_pool_begin(CommandPool *self);
	static VkCommandBuffer command_pool_request_command_buffer(CommandPool *self);
	static inline void command_pool_signal_submitted(CommandPool *self, VkCommandBuffer cmd)
	{
#ifdef VULKAN_DEBUG
		int found = -1;
		int i;
		for (i = 0; i < self->in_flight.count; i++)
			if (self->in_flight.items[i] == cmd) { found = i; break; }
		VK_ASSERT(found >= 0);
		if (found >= 0)
		{
			self->in_flight.items[found] = self->in_flight.items[self->in_flight.count - 1];
			CommandBufferVec_pop_back(&self->in_flight);
		}
#else
		(void)cmd;
#endif
	}

/* ============================================================
 * command_buffer.hpp
 * ============================================================ */

#define COMPARE_OP_BITS 3
#define BLEND_FACTOR_BITS 5
#define BLEND_OP_BITS 3
#define CULL_MODE_BITS 2

	enum CommandBufferDirtyBits
	{
		COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
		COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

		COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
		COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,

		COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,

		COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7,

		COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT
	};
	using CommandBufferDirtyFlags = uint32_t;

	union PipelineState {
		struct State
		{
			// Depth state.
			unsigned depth_write : 1;
			unsigned depth_test : 1;
			unsigned blend_enable : 1;

			unsigned cull_mode : CULL_MODE_BITS;

			unsigned depth_compare : COMPARE_OP_BITS;

			unsigned alpha_to_coverage : 1;
			unsigned alpha_to_one : 1;
			unsigned sample_shading : 1;

			unsigned src_color_blend : BLEND_FACTOR_BITS;
			unsigned dst_color_blend : BLEND_FACTOR_BITS;
			unsigned color_blend_op : BLEND_OP_BITS;
			unsigned src_alpha_blend : BLEND_FACTOR_BITS;
			unsigned dst_alpha_blend : BLEND_FACTOR_BITS;
			unsigned alpha_blend_op : BLEND_OP_BITS;
			unsigned topology : 4;

			unsigned spec_constant_mask : 8;
		} state;
		uint32_t words[4];
	};

	struct PotentialState
	{
		float blend_constants[4];
		uint32_t spec_constants[VULKAN_NUM_SPEC_CONSTANTS];
	};

	struct VertexAttribState
	{
		uint32_t binding;
		VkFormat format;
		uint32_t offset;
	};

	struct VertexBindingState
	{
		VkBuffer buffers[VULKAN_NUM_VERTEX_BUFFERS];
		VkDeviceSize offsets[VULKAN_NUM_VERTEX_BUFFERS];
		VkDeviceSize strides[VULKAN_NUM_VERTEX_BUFFERS];
		VkVertexInputRate input_rates[VULKAN_NUM_VERTEX_BUFFERS];
	};

	struct ResourceBinding
	{
		union {
			VkDescriptorBufferInfo buffer;
			struct
			{
				VkDescriptorImageInfo fp;
				VkDescriptorImageInfo integer;
			} image;
			VkBufferView buffer_view;
		};
	};

	struct ResourceBindings
	{
		ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];
	};

	static_assert(VULKAN_NUM_DESCRIPTOR_SETS == 4, "Number of descriptor sets != 4.");

	/* set_dirty / get_and_clear were small CommandBuffer member helpers used by
	 * many methods (including in-struct inline setters). Declared here, before the
	 * struct, so the still-inline member methods can call them during the staged
	 * conversion; defined after the struct. */
	struct CommandBuffer;
	void commandbuffer_set_dirty(struct CommandBuffer *self, CommandBufferDirtyFlags flags);
	CommandBufferDirtyFlags commandbuffer_get_and_clear(struct CommandBuffer *self, CommandBufferDirtyFlags flags);
	/* Formerly CommandBufferType (a nested enum). Hoisted to file scope so the
	 * forthcoming class -> C struct conversion of CommandBuffer does not have to
	 * carry a nested type. The Type_* enumerator names are already prefixed, so
	 * there is no collision at file scope. */
	enum CommandBufferType {
		Type_Generic,
		Type_AsyncGraphics,
		Type_AsyncCompute,
		Type_AsyncTransfer
	};
	struct CommandBufferDeleter
	{
		void operator()(CommandBuffer *cmd);
	};

	class Device;
	/* Refcount carried as a plain member instead of via the IntrusivePtrEnabled
	 * CRTP base (IntrusivePtr dispatches through the pointee directly). This is the
	 * last of the eight pointees taken off the template base.
	 *
	 * Stage 1 of the class -> C struct conversion: CommandBuffer is now a struct,
	 * the refcount/ctor/dtor are free functions (commandbuffer_init / _fini /
	 * _add_reference / _release_reference), and the formerly-default-initialized
	 * members are seeded in commandbuffer_init. The remaining member functions are
	 * still declared in-struct for now and are converted to free functions in
	 * later stages. */
	struct CommandBuffer
	{
		public:
			friend struct CommandBufferDeleter;
			friend void commandbuffer_set_dirty(struct CommandBuffer *self, CommandBufferDirtyFlags flags);
			friend CommandBufferDirtyFlags commandbuffer_get_and_clear(struct CommandBuffer *self, CommandBufferDirtyFlags flags);
			friend void commandbuffer_init(struct CommandBuffer *self, Device *device, VkCommandBuffer cmd, CommandBufferType type);
			friend void commandbuffer_fini(struct CommandBuffer *self);
			friend void commandbuffer_add_reference(struct CommandBuffer *self);
			friend void commandbuffer_release_reference(struct CommandBuffer *self);
			friend void commandbuffer_clear_image(struct CommandBuffer *self, const Image &image, const VkClearValue &value);
			friend void commandbuffer_copy_buffer(struct CommandBuffer *self, const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset, VkDeviceSize size);
			friend void commandbuffer_copy_buffer_to_image(struct CommandBuffer *self, const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset, const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length, unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
			friend void commandbuffer_copy_buffer_to_image_blits(struct CommandBuffer *self, const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits);
			friend void commandbuffer_copy_image_to_buffer(struct CommandBuffer *self, const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length, unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
			friend void commandbuffer_full_barrier(struct CommandBuffer *self);
			friend void commandbuffer_pixel_barrier(struct CommandBuffer *self);
			friend void commandbuffer_barrier_simple(struct CommandBuffer *self, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
			friend void commandbuffer_barrier(struct CommandBuffer *self, VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers, const VkMemoryBarrier *globals, unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers, unsigned image_barriers, const VkImageMemoryBarrier *images);
			friend void commandbuffer_image_barrier(struct CommandBuffer *self, const Image &image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
			friend void commandbuffer_blit_image(struct CommandBuffer *self, const Image &dst, const Image &src, const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent, const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level, unsigned dst_base_layer, uint32_t src_base_layer, unsigned num_layers, VkFilter filter);
			friend void commandbuffer_barrier_prepare_generate_mipmap(struct CommandBuffer *self, const Image &image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, bool need_top_level_barrier);
			friend void commandbuffer_generate_mipmap(struct CommandBuffer *self, const Image &image);
			friend void commandbuffer_begin_render_pass(struct CommandBuffer *self, const RenderPassInfo &info, VkSubpassContents contents);
			friend void commandbuffer_end_render_pass(struct CommandBuffer *self);
			friend void commandbuffer_set_program(struct CommandBuffer *self, Program &program);
			friend void commandbuffer_set_scissor(struct CommandBuffer *self, const VkRect2D &rect);
			friend void commandbuffer_push_constants(struct CommandBuffer *self, const void *data, VkDeviceSize offset, VkDeviceSize range);
			friend void commandbuffer_flush_render_state(struct CommandBuffer *self);
			friend VkPipeline commandbuffer_build_graphics_pipeline(struct CommandBuffer *self, Hash hash);
			friend VkPipeline commandbuffer_build_compute_pipeline(struct CommandBuffer *self, Hash hash);
			friend void commandbuffer_flush_graphics_pipeline(struct CommandBuffer *self);
			friend void commandbuffer_flush_compute_pipeline(struct CommandBuffer *self);
			friend void commandbuffer_flush_descriptor_sets(struct CommandBuffer *self);
			friend void commandbuffer_flush_descriptor_set(struct CommandBuffer *self, uint32_t set);
			friend void commandbuffer_begin_context(struct CommandBuffer *self);
			friend void commandbuffer_begin_graphics(struct CommandBuffer *self);
			friend void commandbuffer_begin_compute(struct CommandBuffer *self);
			friend void commandbuffer_flush_compute_state(struct CommandBuffer *self);
			friend void commandbuffer_init_viewport_scissor(struct CommandBuffer *self, const RenderPassInfo &info, const Framebuffer *framebuffer);
			friend void commandbuffer_set_buffer_view(struct CommandBuffer *self, unsigned set, unsigned binding, const BufferView &view);
			friend void commandbuffer_set_input_attachments(struct CommandBuffer *self, unsigned set, unsigned start_binding);
			friend void commandbuffer_set_texture_raw(struct CommandBuffer *self, unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view, VkImageLayout layout, uint64_t cookie);
			friend void commandbuffer_set_texture_view(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view);
			friend void commandbuffer_set_texture_view_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
			friend void commandbuffer_set_texture_view_stock(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, StockSampler stock);
			friend void commandbuffer_set_storage_texture(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view);
			friend void commandbuffer_set_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const Sampler &sampler);
			friend void commandbuffer_set_uniform_buffer(struct CommandBuffer *self, unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize range);
			friend void *commandbuffer_allocate_constant_data(struct CommandBuffer *self, unsigned set, unsigned binding, VkDeviceSize size);
			friend void *commandbuffer_allocate_vertex_data(struct CommandBuffer *self, unsigned binding, VkDeviceSize size, VkDeviceSize stride, VkVertexInputRate step_rate);
			friend VkCommandBuffer commandbuffer_get_command_buffer(const struct CommandBuffer *self);
			friend Device &commandbuffer_get_device(struct CommandBuffer *self);
			friend void commandbuffer_set_viewport(struct CommandBuffer *self, const VkViewport &viewport);
			friend const VkViewport &commandbuffer_get_viewport(const struct CommandBuffer *self);
			friend void commandbuffer_set_vertex_attrib(struct CommandBuffer *self, uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
			friend void commandbuffer_set_vertex_binding(struct CommandBuffer *self, uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate step_rate);
			friend void commandbuffer_set_opaque_state(struct CommandBuffer *self);
			friend void commandbuffer_set_quad_state(struct CommandBuffer *self);
			friend void commandbuffer_set_depth_test(struct CommandBuffer *self, bool depth_test, bool depth_write);
			friend void commandbuffer_set_depth_compare(struct CommandBuffer *self, VkCompareOp depth_compare);
			friend void commandbuffer_set_blend_enable(struct CommandBuffer *self, bool blend_enable);
			friend void commandbuffer_set_blend_factors(struct CommandBuffer *self, VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend, VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend);
			friend void commandbuffer_set_blend_op(struct CommandBuffer *self, VkBlendOp color_blend_op, VkBlendOp alpha_blend_op);
			friend void commandbuffer_set_primitive_topology(struct CommandBuffer *self, VkPrimitiveTopology topology);
			friend void commandbuffer_set_multisample_state(struct CommandBuffer *self, bool alpha_to_coverage, bool alpha_to_one, bool sample_shading);
			friend void commandbuffer_set_cull_mode(struct CommandBuffer *self, VkCullModeFlags cull_mode);
			friend void commandbuffer_set_blend_constants(struct CommandBuffer *self, const float blend_constants[4]);
			friend void commandbuffer_set_specialization_constant_mask(struct CommandBuffer *self, uint32_t spec_constant_mask);
			friend void commandbuffer_set_specialization_constant(struct CommandBuffer *self, unsigned index, uint32_t value);
			friend CommandBufferType commandbuffer_get_command_buffer_type(const struct CommandBuffer *self);
			friend void commandbuffer_draw(struct CommandBuffer *self, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);
			friend void commandbuffer_dispatch(struct CommandBuffer *self, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
			friend void commandbuffer_begin_region(struct CommandBuffer *self, const char *name, const float *color);
			friend void commandbuffer_end_region(struct CommandBuffer *self);
			friend void commandbuffer_end(struct CommandBuffer *self);




			/* Group A (copy/clear/barrier) methods are now free functions. */











			/* Group D state setters (incl. SET_STATIC_STATE macros) are now free functions. */


		private:
			friend class Device;

			Device *device;
			VkCommandBuffer cmd;
			CommandBufferType type;

			const Framebuffer *framebuffer;
			const RenderPass *actual_render_pass;
			const RenderPass *compatible_render_pass;

			VertexAttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS];
			VertexBindingState vbo;
			ResourceBindings bindings;

			VkPipeline current_pipeline;
			VkPipelineLayout current_pipeline_layout;
			PipelineLayout *current_layout;
			Program *current_program;
			unsigned current_subpass;
			VkSubpassContents current_contents;

			VkViewport viewport;
			VkRect2D scissor;

			CommandBufferDirtyFlags dirty;
			uint32_t dirty_sets;
			uint32_t dirty_vbos;
			uint32_t active_vbos;
			bool is_compute;


			PipelineState static_state;
			PotentialState potential_static_state;
#ifndef _MSC_VER
			static_assert(sizeof(static_state.words) >= sizeof(static_state.state),
					"Hashable pipeline state is not large enough!");
#endif



			BufferBlock vbo_block;
			BufferBlock ubo_block;



			HandleCounter reference_count;
	};

	/* Stage 1 free functions for the CommandBuffer pointee. The remaining member
	 * functions still declared in the struct are converted in later stages. */
	void commandbuffer_init(struct CommandBuffer *self, Device *device, VkCommandBuffer cmd, CommandBufferType type);
	void commandbuffer_fini(struct CommandBuffer *self);
	void commandbuffer_add_reference(struct CommandBuffer *self);
	void commandbuffer_release_reference(struct CommandBuffer *self);
	inline void commandbuffer_set_dirty(struct CommandBuffer *self, CommandBufferDirtyFlags flags) { self->dirty |= flags; }
	inline CommandBufferDirtyFlags commandbuffer_get_and_clear(struct CommandBuffer *self, CommandBufferDirtyFlags flags) { CommandBufferDirtyFlags mask = self->dirty & flags; self->dirty &= ~flags; return mask; }

	/* Group B: render-pass / pipeline / descriptor-flush free functions. */
	void commandbuffer_begin_render_pass(struct CommandBuffer *self, const RenderPassInfo &info, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
	void commandbuffer_end_render_pass(struct CommandBuffer *self);
	void commandbuffer_set_program(struct CommandBuffer *self, Program &program);
	void commandbuffer_set_scissor(struct CommandBuffer *self, const VkRect2D &rect);
	void commandbuffer_push_constants(struct CommandBuffer *self, const void *data, VkDeviceSize offset, VkDeviceSize range);
	void commandbuffer_flush_render_state(struct CommandBuffer *self);
	VkPipeline commandbuffer_build_graphics_pipeline(struct CommandBuffer *self, Hash hash);
	VkPipeline commandbuffer_build_compute_pipeline(struct CommandBuffer *self, Hash hash);
	void commandbuffer_flush_graphics_pipeline(struct CommandBuffer *self);
	void commandbuffer_flush_compute_pipeline(struct CommandBuffer *self);
	void commandbuffer_flush_descriptor_sets(struct CommandBuffer *self);
	void commandbuffer_flush_descriptor_set(struct CommandBuffer *self, uint32_t set);
	void commandbuffer_begin_context(struct CommandBuffer *self);
	void commandbuffer_flush_compute_state(struct CommandBuffer *self);
	void commandbuffer_init_viewport_scissor(struct CommandBuffer *self, const RenderPassInfo &info, const Framebuffer *framebuffer);
	inline void commandbuffer_begin_graphics(struct CommandBuffer *self) { self->is_compute = false; commandbuffer_begin_context(self); }
	inline void commandbuffer_begin_compute(struct CommandBuffer *self) { self->is_compute = true; commandbuffer_begin_context(self); }

	/* Group C: descriptor / resource-bind free functions. The 4 set_texture
	 * overloads become distinctly-named free functions. */
	void commandbuffer_set_buffer_view(struct CommandBuffer *self, unsigned set, unsigned binding, const BufferView &view);
	void commandbuffer_set_input_attachments(struct CommandBuffer *self, unsigned set, unsigned start_binding);
	void commandbuffer_set_texture_raw(struct CommandBuffer *self, unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view, VkImageLayout layout, uint64_t cookie);
	void commandbuffer_set_texture_view(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view);
	void commandbuffer_set_texture_view_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler);
	void commandbuffer_set_texture_view_stock(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, StockSampler stock);
	void commandbuffer_set_storage_texture(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view);
	void commandbuffer_set_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const Sampler &sampler);
	void commandbuffer_set_uniform_buffer(struct CommandBuffer *self, unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize range);
	void *commandbuffer_allocate_constant_data(struct CommandBuffer *self, unsigned set, unsigned binding, VkDeviceSize size);
	void *commandbuffer_allocate_vertex_data(struct CommandBuffer *self, unsigned binding, VkDeviceSize size, VkDeviceSize stride, VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);

	/* Group D: state-setter free functions. The SET_STATIC_STATE /
	 * SET_POTENTIALLY_STATIC_STATE macros are redefined to operate on self. */
#undef SET_STATIC_STATE
#undef SET_POTENTIALLY_STATIC_STATE
#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (self->static_state.state.value != value)          \
		{                                                     \
			self->static_state.state.value = value;           \
			commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)
#define SET_POTENTIALLY_STATIC_STATE(value)                   \
	do                                                        \
	{                                                         \
		if (self->potential_static_state.value != value)      \
		{                                                     \
			self->potential_static_state.value = value;       \
			commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

	void commandbuffer_set_vertex_attrib(struct CommandBuffer *self, uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
	void commandbuffer_set_vertex_binding(struct CommandBuffer *self, uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
	void commandbuffer_set_opaque_state(struct CommandBuffer *self);
	void commandbuffer_set_quad_state(struct CommandBuffer *self);

	inline VkCommandBuffer commandbuffer_get_command_buffer(const struct CommandBuffer *self) { return self->cmd; }
	inline Device &commandbuffer_get_device(struct CommandBuffer *self) { return *self->device; }
	inline const VkViewport &commandbuffer_get_viewport(const struct CommandBuffer *self) { return self->viewport; }
	inline CommandBufferType commandbuffer_get_command_buffer_type(const struct CommandBuffer *self) { return self->type; }

	/* Group E: draw / dispatch / debug-region / end free functions. */
	void commandbuffer_draw(struct CommandBuffer *self, uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
	void commandbuffer_dispatch(struct CommandBuffer *self, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
	void commandbuffer_begin_region(struct CommandBuffer *self, const char *name, const float *color = NULL);
	void commandbuffer_end_region(struct CommandBuffer *self);
	void commandbuffer_end(struct CommandBuffer *self);
	inline void commandbuffer_set_viewport(struct CommandBuffer *self, const VkViewport &viewport)
	{
		VK_ASSERT(self->framebuffer);
		self->viewport = viewport;
		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
	}
	inline void commandbuffer_set_depth_test(struct CommandBuffer *self, bool depth_test, bool depth_write)
	{
		SET_STATIC_STATE(depth_test);
		SET_STATIC_STATE(depth_write);
	}
	inline void commandbuffer_set_depth_compare(struct CommandBuffer *self, VkCompareOp depth_compare)
	{
		SET_STATIC_STATE(depth_compare);
	}
	inline void commandbuffer_set_blend_enable(struct CommandBuffer *self, bool blend_enable)
	{
		SET_STATIC_STATE(blend_enable);
	}
	inline void commandbuffer_set_blend_factors(struct CommandBuffer *self, VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend, VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
	{
		SET_STATIC_STATE(src_color_blend);
		SET_STATIC_STATE(dst_color_blend);
		SET_STATIC_STATE(src_alpha_blend);
		SET_STATIC_STATE(dst_alpha_blend);
	}
	static inline void commandbuffer_set_blend_factors_2(struct CommandBuffer *self, VkBlendFactor src_blend, VkBlendFactor dst_blend)
	{
		commandbuffer_set_blend_factors(self, src_blend, src_blend, dst_blend, dst_blend);
	}
	inline void commandbuffer_set_blend_op(struct CommandBuffer *self, VkBlendOp color_blend_op, VkBlendOp alpha_blend_op)
	{
		SET_STATIC_STATE(color_blend_op);
		SET_STATIC_STATE(alpha_blend_op);
	}
	static inline void commandbuffer_set_blend_op_2(struct CommandBuffer *self, VkBlendOp blend_op)
	{
		commandbuffer_set_blend_op(self, blend_op, blend_op);
	}
	inline void commandbuffer_set_primitive_topology(struct CommandBuffer *self, VkPrimitiveTopology topology)
	{
		SET_STATIC_STATE(topology);
	}
	inline void commandbuffer_set_multisample_state(struct CommandBuffer *self, bool alpha_to_coverage, bool alpha_to_one = false, bool sample_shading = false)
	{
		SET_STATIC_STATE(alpha_to_coverage);
		SET_STATIC_STATE(alpha_to_one);
		SET_STATIC_STATE(sample_shading);
	}
	inline void commandbuffer_set_cull_mode(struct CommandBuffer *self, VkCullModeFlags cull_mode)
	{
		SET_STATIC_STATE(cull_mode);
	}
	inline void commandbuffer_set_blend_constants(struct CommandBuffer *self, const float blend_constants[4])
	{
		SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
		SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
	}
	inline void commandbuffer_set_specialization_constant_mask(struct CommandBuffer *self, uint32_t spec_constant_mask)
	{
		VK_ASSERT((spec_constant_mask & ~((1u << VULKAN_NUM_SPEC_CONSTANTS) - 1u)) == 0u);
		SET_STATIC_STATE(spec_constant_mask);
	}
	inline void commandbuffer_set_specialization_constant(struct CommandBuffer *self, unsigned index, uint32_t value)
	{
		VK_ASSERT(index < VULKAN_NUM_SPEC_CONSTANTS);
		if (memcmp(&self->potential_static_state.spec_constants[index], &value, sizeof(value)))
		{
			memcpy(&self->potential_static_state.spec_constants[index], &value, sizeof(value));
			if (self->static_state.state.spec_constant_mask & (1u << index))
				commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
		}
	}

	/* Group A: copy / clear / barrier free functions. Overloaded members are
	 * split into distinctly-named free functions. */
	void commandbuffer_clear_image(struct CommandBuffer *self, const Image &image, const VkClearValue &value);
	void commandbuffer_copy_buffer(struct CommandBuffer *self, const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset, VkDeviceSize size);
	static inline void commandbuffer_copy_buffer_whole(struct CommandBuffer *self, const Buffer &dst, const Buffer &src)
	{
		VK_ASSERT(buffer_get_create_info(&dst).size == buffer_get_create_info(&src).size);
		commandbuffer_copy_buffer(self, dst, 0, src, 0, buffer_get_create_info(&dst).size);
	}
	void commandbuffer_copy_buffer_to_image(struct CommandBuffer *self, const Image &image, const Buffer &buffer, VkDeviceSize buffer_offset, const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length, unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
	void commandbuffer_copy_buffer_to_image_blits(struct CommandBuffer *self, const Image &image, const Buffer &buffer, unsigned num_blits, const VkBufferImageCopy *blits);
	void commandbuffer_copy_image_to_buffer(struct CommandBuffer *self, const Buffer &dst, const Image &src, VkDeviceSize buffer_offset, const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length, unsigned slice_height, const VkImageSubresourceLayers &subresrouce);
	void commandbuffer_full_barrier(struct CommandBuffer *self);
	void commandbuffer_pixel_barrier(struct CommandBuffer *self);
	void commandbuffer_barrier_simple(struct CommandBuffer *self, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
	void commandbuffer_barrier(struct CommandBuffer *self, VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers, const VkMemoryBarrier *globals, unsigned buffer_barriers, const VkBufferMemoryBarrier *buffers, unsigned image_barriers, const VkImageMemoryBarrier *images);
	void commandbuffer_image_barrier(struct CommandBuffer *self, const Image &image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
	void commandbuffer_blit_image(struct CommandBuffer *self, const Image &dst, const Image &src, const VkOffset3D &dst_offset0, const VkOffset3D &dst_extent, const VkOffset3D &src_offset0, const VkOffset3D &src_extent, unsigned dst_level, unsigned src_level, unsigned dst_base_layer, uint32_t src_base_layer, unsigned num_layers, VkFilter filter);
	void commandbuffer_barrier_prepare_generate_mipmap(struct CommandBuffer *self, const Image &image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, bool need_top_level_barrier);
	void commandbuffer_generate_mipmap(struct CommandBuffer *self, const Image &image);


	/* Command-buffer handle: de-RAII'd from
	 * INTRUSIVE_HANDLE_DECLARE(CommandBufferHandle, CommandBuffer) to a plain
	 * struct + explicit free functions, following the established template. The
	 * pointee CommandBuffer is unchanged. Held as the Renderer's current-cmd
	 * member, by-value locals, and a move-only CommandBufferHandleVec (the
	 * per-queue submission list). cbh_steal implements the move the macro's
	 * move-ctor did; cbh_reset the dtor decref; cbh_move a release-old/
	 * take-produced member reassignment.
	 * ASAN-GATE: per-frame command-buffer submission lifetime. */
	struct CommandBufferHandle { CommandBuffer *data; };
	static inline struct CommandBufferHandle cbh_make(CommandBuffer *p) { struct CommandBufferHandle h; h.data = p; return h; }
	static inline void cbh_reset(struct CommandBufferHandle *h) { if (h->data) commandbuffer_release_reference(h->data); h->data = NULL; }
	static inline CommandBuffer *cbh_get(const struct CommandBufferHandle *h) { return h->data; }
	static inline int cbh_is_valid(const struct CommandBufferHandle *h) { return h->data != NULL; }
	static inline void cbh_steal(struct CommandBufferHandle *dst, struct CommandBufferHandle *src) { dst->data = src->data; src->data = NULL; }
	static inline void cbh_move(struct CommandBufferHandle *dst, struct CommandBufferHandle produced) {
		if (dst->data) commandbuffer_release_reference(dst->data);
		dst->data = produced.data;
	}

/* ============================================================
 * device.hpp
 * ============================================================ */

	struct InitialImageBuffer
	{
		BufferHandle buffer = { NULL };
		// Bound matches the implicit invariant in TextureFormatLayout::mips[16]:
		// callers must pass <= 16 mip levels (no runtime check exists in
		// fill_mipinfo). build_buffer_image_copies is the sole writer of these
		// fields; it asserts num_blits <= 16 before writing.
		VkBufferImageCopy blits[16];
		unsigned num_blits = 0;
	};
	struct HandlePool
	{
		struct ObjectPoolRaw buffers;
		struct ObjectPoolRaw images;
		struct ObjectPoolRaw image_views;
		struct ObjectPoolRaw buffer_views;
		struct ObjectPoolRaw samplers;
		struct ObjectPoolRaw fences;
		struct ObjectPoolRaw semaphores;
		struct ObjectPoolRaw command_buffers;

		/* For a malloc'd owner (Device), where the pools' constructors and
		 * destructors do not run: init() puts every pool in the empty state and
		 * deinit() runs each pool's teardown (free pooled nodes and slabs). */
		void init()
		{
			object_pool_raw_init(&buffers,         sizeof(Buffer));
			object_pool_raw_init(&images,          sizeof(Image));
			object_pool_raw_init(&image_views,     sizeof(ImageView));
			object_pool_raw_init(&buffer_views,    sizeof(BufferView));
			object_pool_raw_init(&samplers,        sizeof(Sampler));
			object_pool_raw_init(&fences,          sizeof(FenceHolder));
			object_pool_raw_init(&semaphores,      sizeof(SemaphoreHolder));
			object_pool_raw_init(&command_buffers, sizeof(CommandBuffer));
		}

		void deinit()
		{
			object_pool_raw_deinit(&buffers);
			object_pool_raw_deinit(&images);
			object_pool_raw_deinit(&image_views);
			object_pool_raw_deinit(&buffer_views);
			object_pool_raw_deinit(&samplers);
			object_pool_raw_deinit(&fences);
			object_pool_raw_deinit(&semaphores);
			object_pool_raw_deinit(&command_buffers);
		}
	};

	/* Thread-locking primitives used by the Device implementation
	 * below. Defined here (instead of just before device.cpp's
	 * out-of-line bodies further down the TU) so the Device class
	 * declaration's inline methods can use them too. */

	/* Per-frame deletion/recycle queues (plain Vulkan handles, POD). Hoisted to
	 * file scope from inside Device so the per_frame_* free functions (and other
	 * non-member code) can use the generated accessors. */
	POD_VEC_DECLARE(VkFramebufferVec, VkFramebuffer);
	POD_VEC_DECLARE(VkSamplerVec, VkSampler);
	POD_VEC_DECLARE(VkPipelineVec, VkPipeline);
	POD_VEC_DECLARE(VkBufferViewVec, VkBufferView);
	POD_VEC_DECLARE(VkImageVec, VkImage);
	POD_VEC_DECLARE(VkBufferVec, VkBuffer);
	POD_VEC_DECLARE(VkPipelineStageVec, VkPipelineStageFlags);

	class Device
	{
		public:
			// Device-based objects which need to poke at internal data structures when their lifetimes end.
			// Don't want to expose a lot of internal guts to make this work.
			friend struct SemaphoreHolderDeleter;
			friend void semaphoreholder_fini(struct SemaphoreHolder *self);
			friend struct FenceHolderDeleter;
			friend void fenceholder_fini(struct FenceHolder *self);
			friend struct SamplerDeleter;
			friend void sampler_fini(struct Sampler *self);
			friend struct BufferDeleter;
			friend void buffer_fini(struct Buffer *self);
			friend struct BufferViewDeleter;
			friend void bufferview_fini(struct BufferView *self);
			friend struct ImageViewDeleter;
			friend void imageview_fini(struct ImageView *self);
			friend struct ImageDeleter;
			friend void image_fini(struct Image *self);
			friend void image_init(struct Image *self, Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc, const ImageCreateInfo &info);
			friend struct CommandBufferDeleter;
			friend void commandbuffer_begin_render_pass(struct CommandBuffer *self, const RenderPassInfo &info, VkSubpassContents contents);
			friend void commandbuffer_end_render_pass(struct CommandBuffer *self);
			friend void commandbuffer_set_program(struct CommandBuffer *self, Program &program);
			friend void commandbuffer_flush_render_state(struct CommandBuffer *self);
			friend VkPipeline commandbuffer_build_graphics_pipeline(struct CommandBuffer *self, Hash hash);
			friend VkPipeline commandbuffer_build_compute_pipeline(struct CommandBuffer *self, Hash hash);
			friend void commandbuffer_flush_graphics_pipeline(struct CommandBuffer *self);
			friend void commandbuffer_flush_compute_pipeline(struct CommandBuffer *self);
			friend void commandbuffer_flush_descriptor_sets(struct CommandBuffer *self);
			friend void commandbuffer_flush_descriptor_set(struct CommandBuffer *self, uint32_t set);
			friend void commandbuffer_begin_context(struct CommandBuffer *self);
			friend void commandbuffer_flush_compute_state(struct CommandBuffer *self);
			friend void commandbuffer_init_viewport_scissor(struct CommandBuffer *self, const RenderPassInfo &info, const Framebuffer *framebuffer);
			friend void commandbuffer_begin_region(struct CommandBuffer *self, const char *name, const float *color);
			friend void commandbuffer_end_region(struct CommandBuffer *self);
			friend void commandbuffer_end(struct CommandBuffer *self);
			friend void *commandbuffer_allocate_constant_data(struct CommandBuffer *self, unsigned set, unsigned binding, VkDeviceSize size);
			friend void *commandbuffer_allocate_vertex_data(struct CommandBuffer *self, unsigned binding, VkDeviceSize size, VkDeviceSize stride, VkVertexInputRate step_rate);
			friend class Program;
			friend class Framebuffer;
			friend class PipelineLayout;
			friend class FramebufferAllocator;

			/* Lifecycle for a malloc'd Device (no constructor/destructor runs).
			 * device_init establishes every member's empty state; device_deinit
			 * tears the device down so the storage can be freed. Static so they take
			 * the raw Device* explicitly. */
			static void device_init(Device *self);
			static void device_deinit(Device *self);

			// No move-copy.
			void operator=(Device &&) = delete;
			Device(Device &&) = delete;

			// Only called by main thread, during setup phase.
			void set_context(const Context &context);
			void init_frame_contexts(unsigned count);

			// Frame-pushing interface.
			void next_frame_context();
			void wait_idle()
			{
				wait_idle_nolock();
			}

			// Set names for objects for debuggers and profilers.
			void set_name(const Buffer &buffer, const char *name);
			void set_name(const Image &image, const char *name);

			// Submission interface, may be called from any thread at any time.
			void flush_frame()
			{
				flush_frame_nolock();
			}
			CommandBufferHandle request_command_buffer(CommandBufferType type = Type_Generic);
			void submit(CommandBufferHandle &cmd, Fence *fence = NULL,
					unsigned semaphore_count = 0, Semaphore *semaphore = NULL);
			CommandBufferType get_physical_queue_type(CommandBufferType queue_type) const;

			// Request shaders and programs. These objects are owned by the Device.
			Shader *request_shader(const uint32_t *code, size_t size);
			Program *request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
					size_t fragment_size);
			Program *request_program(const uint32_t *compute_data, size_t compute_size);
			Program *request_program(Shader *vertex, Shader *fragment);
			Program *request_program(Shader *compute);

			// Map and unmap buffer objects.
			void *map_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
			{
				return deviceallocator_map_memory(&managers.memory, &buffer_get_allocation(&buffer), access);
			}
			void unmap_host_buffer(const Buffer &buffer, MemoryAccessFlags access)
			{
				deviceallocator_unmap_memory(&managers.memory, &buffer_get_allocation(&buffer), access);
			}

			// Create buffers and images.
			BufferHandle create_buffer(const BufferCreateInfo &info, const void *initial = NULL);
			ImageHandle create_image(const ImageCreateInfo &info, const ImageInitialData *initial = NULL);
			ImageHandle create_image_from_staging_buffer(const ImageCreateInfo &info, const InitialImageBuffer *buffer);

			// Create staging buffers for images.
			InitialImageBuffer create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial);

			// Create image view, buffer views and samplers.
			ImageViewHandle create_image_view(const ImageViewCreateInfo &view_info);
			BufferViewHandle create_buffer_view(const BufferViewCreateInfo &view_info);

			// Render pass helpers.
			bool image_format_is_supported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
			bool get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
					VkImageFormatProperties *properties);

			VkFormat get_default_depth_format() const;
			ImageView &get_transient_attachment(unsigned width, unsigned height, VkFormat format,
					unsigned index = 0, unsigned samples = 1, unsigned layers = 1)
			{
				return *attachment_allocator_request_attachment(&transient_allocator, width, height, format, index, samples, layers);
			}

			VkDevice get_device()
			{
				return device;
			}

			const VkPhysicalDeviceProperties &get_gpu_properties() const
			{
				return gpu_props;
			}

			const Sampler &get_stock_sampler(StockSampler sampler) const
			{
				return *smh_get(&samplers[(unsigned)(sampler)]);
			}


			const ImplementationWorkarounds &get_workarounds() const
			{
				return workarounds;
			}

			const DeviceFeatures &get_device_features() const
			{
				return ext;
			}

		private:
			VkInstance instance = VK_NULL_HANDLE;
			VkPhysicalDevice gpu = VK_NULL_HANDLE;
			VkDevice device = VK_NULL_HANDLE;
			VkQueue graphics_queue = VK_NULL_HANDLE;
			VkQueue compute_queue = VK_NULL_HANDLE;
			VkQueue transfer_queue = VK_NULL_HANDLE;

			uint64_t cookie = 0;

		public:
			/* Public so the C89 cookie_init() free function (replacing the
			 * former Cookie(Device*) ctor + friendship) can reach it. */
			uint64_t allocate_cookie()
			{
				// Reserve lower bits for "special purposes".
				cookie += 16;
				return cookie;
			}
		private:
			void bake_program(Program &program);
			friend void program_init_graphics(struct Program *self, Device *device, Shader *vertex, Shader *fragment);
			friend void program_init_compute(struct Program *self, Device *device, Shader *compute);

			void request_vertex_block(BufferBlock &block, VkDeviceSize size)
			{
				request_vertex_block_nolock(block, size);
			}
			void request_uniform_block(BufferBlock &block, VkDeviceSize size)
			{
				request_uniform_block_nolock(block, size);
			}

			PipelineLayout *request_pipeline_layout(const CombinedResourceLayout &layout);
			DescriptorSetAllocator *request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_sets);
			friend void pipeline_layout_init(struct PipelineLayout *self, Hash hash, Device *device, const CombinedResourceLayout &layout);
			const Framebuffer &request_framebuffer(const RenderPassInfo &info)
			{
				return *framebuffer_allocator_request_framebuffer(&framebuffer_allocator, info);
			}
			const RenderPass &request_render_pass(const RenderPassInfo &info, bool compatible);
			friend struct Framebuffer *framebuffer_allocator_request_framebuffer(struct FramebufferAllocator *self, const RenderPassInfo &info);

			VkPhysicalDeviceMemoryProperties mem_props;
			VkPhysicalDeviceProperties gpu_props;

			DeviceFeatures ext;
			void init_stock_samplers();

			// Make sure this is deleted last.
			HandlePool handle_pool;

			struct Managers
			{
				DeviceAllocator memory;
				FenceManager fence;
				SemaphoreManager semaphore;
				BufferPool vbo, ubo;
			};
			Managers managers;

			struct
			{
				unsigned counter = 0;
			} lock;


			/* Owning array of CommandBufferHandle (= IntrusivePtr<CommandBuffer>).
			 * Replaces std::vector<CommandBufferHandle> for the per-queue submission
			 * lists. The element is a refcounting smart pointer with a non-trivial
			 * destructor (decref), so growth move-constructs each handle into new
			 * storage with placement new and destroys the moved-from slot, and
			 * clear()/the destructor run each handle's destructor (dropping its
			 * ref). push takes ownership by move (no incref), matching the old
			 * push_back(std::move(cmd)). clear() keeps the capacity so the lists
			 * are reused across frames without reallocating. */
			struct CommandBufferHandleVec {
				CommandBufferHandle *items;
				int count;
				int cap;
			};
			friend void cbhvec_init(struct Device::CommandBufferHandleVec *v);
			friend void cbhvec_grow(struct Device::CommandBufferHandleVec *v, int ncap);
			friend void cbhvec_push(struct Device::CommandBufferHandleVec *v, CommandBufferHandle *e);
			friend bool cbhvec_empty(const struct Device::CommandBufferHandleVec *v);
			friend void cbhvec_clear(struct Device::CommandBufferHandleVec *v);
			friend void cbhvec_deinit(struct Device::CommandBufferHandleVec *v);

			struct PerFrame
			{
				VkDevice device;
				Managers *managers;
				CommandPool graphics_cmd_pool;
				CommandPool compute_cmd_pool;
				CommandPool transfer_cmd_pool;

				BufferBlockVec vbo_blocks;
				BufferBlockVec ubo_blocks;

				FenceVec wait_fences;
				FenceVec recycle_fences;
				DeviceAllocationVec allocations;
				VkFramebufferVec destroyed_framebuffers;
				VkSamplerVec destroyed_samplers;
				VkPipelineVec destroyed_pipelines;
				RenderTargetViewVec destroyed_image_views;
				VkBufferViewVec destroyed_buffer_views;
				VkImageVec destroyed_images;
				VkBufferVec destroyed_buffers;
				CommandBufferHandleVec graphics_submissions;
				CommandBufferHandleVec compute_submissions;
				CommandBufferHandleVec transfer_submissions;
				SemaphoreVec recycled_semaphores;
				SemaphoreVec destroyed_semaphores;
			};
			friend void per_frame_init(struct Device::PerFrame *self, Device *device);
			friend void per_frame_fini(struct Device::PerFrame *self);
			friend void per_frame_begin(struct Device::PerFrame *self);
			/* Owning array of PerFrame* (the frame-context ring). Replaces
			 * std::vector<std::unique_ptr<PerFrame>>: the container owns each
			 * heap-allocated PerFrame and deletes it on clear()/destruction, so
			 * ownership is explicit new/delete instead of unique_ptr. Pointers are
			 * trivially relocatable, so the backing array grows by realloc.
			 * push() takes ownership of an already-new'd PerFrame; operator[]
			 * returns the raw pointer (so frame() can dereference it and the
			 * null-asserts still work), and begin/end iterate the pointers. The
			 * default constructor zero-initialises the members (it is not a bare
			 * POD_VEC), so a default-constructed ring is valid before build(). */
			struct PerFramePtrVec {
				PerFrame **items;
				int count;
				int cap;
			};
			friend void per_frame_ptr_vec_init_empty(struct Device::PerFramePtrVec *v);
			friend void per_frame_ptr_vec_deinit(struct Device::PerFramePtrVec *v);
			friend void per_frame_ptr_vec_push(struct Device::PerFramePtrVec *v, struct Device::PerFrame *p);
			friend void per_frame_ptr_vec_clear(struct Device::PerFramePtrVec *v);
			// The per frame structure must be destroyed after
			// the hashmap data structures below, so it must be declared before.
			PerFramePtrVec per_frame;

			struct QueueData
			{
				SemaphoreHandleVec wait_semaphores;
				VkPipelineStageVec wait_stages;
				bool need_fence;
			} graphics, compute, transfer;

			// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
			struct
			{
				BufferBlockVec vbo;
				BufferBlockVec ubo;
			} dma;

			void submit_queue(CommandBufferType type, VkFence *fence,
					unsigned semaphore_count = 0,
					Semaphore *semaphore = NULL);

			PerFrame &frame()
			{
				VK_ASSERT(frame_context_index < per_frame.count);
				VK_ASSERT(per_frame.items[frame_context_index]);
				return *per_frame.items[frame_context_index];
			}

			const PerFrame &frame() const
			{
				VK_ASSERT(frame_context_index < per_frame.count);
				VK_ASSERT(per_frame.items[frame_context_index]);
				return *per_frame.items[frame_context_index];
			}

			unsigned frame_context_index = 0;
			uint32_t graphics_queue_family_index = 0;
			uint32_t compute_queue_family_index = 0;
			uint32_t transfer_queue_family_index = 0;

			uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
			uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
			bool memory_type_is_host_visible(uint32_t type) const
			{
				return (mem_props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
			}

			SamplerHandle samplers[(unsigned)(StockSampler_Count)];

			struct pipeline_layout_map pipeline_layouts;
			struct descriptor_set_allocator_map descriptor_set_allocators;
			struct render_pass_map render_passes;
			struct shader_map shaders;
			struct program_map programs;

			FramebufferAllocator framebuffer_allocator;
			AttachmentAllocator transient_allocator;

			SamplerHandle create_sampler(const SamplerCreateInfo &info, StockSampler sampler);

			CommandPool *get_command_pool(CommandBufferType type);
			QueueData &get_queue_data(CommandBufferType type);
			CommandBufferHandleVec *get_queue_submissions(CommandBufferType type);
			void clear_wait_semaphores();
			void submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush);

			void flush_frame(CommandBufferType type)
			{
				if (type == Type_AsyncTransfer)
					sync_buffer_blocks();
				submit_queue(type, NULL, 0, NULL);
			}
			void sync_buffer_blocks();
			void submit_empty_inner(CommandBufferType type, VkFence *fence,
					unsigned semaphore_count,
					Semaphore *semaphore);

			void reset_fence(VkFence fence);

			void destroy_buffer_nolock(VkBuffer buffer);
			void destroy_image_nolock(VkImage image);
			void destroy_image_view_nolock(VkImageView view);
			void destroy_buffer_view_nolock(VkBufferView view);
			void destroy_pipeline_nolock(VkPipeline pipeline);
			friend void program_fini(struct Program *self);
			void destroy_sampler_nolock(VkSampler sampler);
			void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
			friend void framebuffer_fini(struct Framebuffer *self);
			void destroy_semaphore_nolock(VkSemaphore semaphore);
			void recycle_semaphore_nolock(VkSemaphore semaphore)
			{
				semaphoremanager_recycle(&managers.semaphore, semaphore);
			}
			void free_memory_nolock(const DeviceAllocation &alloc);

			void flush_frame_nolock();
			CommandBufferHandle request_command_buffer_nolock(CommandBufferType type = Type_Generic);
			void submit_nolock(CommandBufferHandle cmd, Fence *fence,
					unsigned semaphore_count, Semaphore *semaphore);
			void add_wait_semaphore_nolock(CommandBufferType type, Semaphore semaphore, VkPipelineStageFlags stages,
					bool flush);

			void request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size);
			void request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size);

			void add_frame_counter_nolock()
			{
				lock.counter++;
			}
			void decrement_frame_counter_nolock()
			{
				VK_ASSERT(lock.counter > 0);
				lock.counter--;
			}
			void wait_idle_nolock();
			void end_frame_nolock();

			ImplementationWorkarounds workarounds;
			void init_workarounds()
			{
				// srcStageMask = ALL_GRAPHICS_BIT causes some weird stalls compared to waiting for fragment only.
				workarounds.optimize_all_graphics_barrier = gpu_props.vendorID == VENDOR_ID_ARM;
			}
	};

	/* Free functions for Device::CommandBufferHandleVec (converted from the move-
	 * only nested container's methods). The element is a refcounting CommandBuffer
	 * handle, so growth/teardown go through cbh_steal/cbh_reset. push() takes
	 * ownership by move (steal, no incref); clear() drops each handle's ref but
	 * keeps capacity; deinit() frees the backing storage. */
	inline void cbhvec_init(struct Device::CommandBufferHandleVec *v)
	{
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	inline void cbhvec_grow(struct Device::CommandBufferHandleVec *v, int ncap)
	{
		CommandBufferHandle *nitems = (CommandBufferHandle *)malloc((size_t)ncap * sizeof(CommandBufferHandle));
		int i;
		for (i = 0; i < v->count; i++)
			cbh_steal(&nitems[i], &v->items[i]);
		free(v->items);
		v->items = nitems;
		v->cap = ncap;
	}
	inline void cbhvec_push(struct Device::CommandBufferHandleVec *v, CommandBufferHandle *e)
	{
		if (v->count >= v->cap)
			cbhvec_grow(v, v->cap ? v->cap * 2 : 8);
		cbh_steal(&v->items[v->count], e);
		v->count++;
	}
	inline bool cbhvec_empty(const struct Device::CommandBufferHandleVec *v) { return v->count == 0; }
	inline void cbhvec_clear(struct Device::CommandBufferHandleVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++)
			cbh_reset(&v->items[i]);
		v->count = 0;
	}
	inline void cbhvec_deinit(struct Device::CommandBufferHandleVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++)
			cbh_reset(&v->items[i]);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}

	/* Free functions for Device::PerFramePtrVec -- the owning frame-context ring
	 * (was std::vector<unique_ptr<PerFrame>>, then a move-only nested container).
	 * The vector owns each heap-allocated PerFrame and per_frame_fini+free's it on
	 * clear()/teardown. Device is malloc'd so the container's own ctor/dtor never
	 * run; init_empty establishes the empty state and deinit performs teardown. */
	inline void per_frame_ptr_vec_init_empty(struct Device::PerFramePtrVec *v)
	{
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	inline void per_frame_ptr_vec_clear(struct Device::PerFramePtrVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++) {
			per_frame_fini(v->items[i]);
			free(v->items[i]);
		}
		v->count = 0;
	}
	inline void per_frame_ptr_vec_deinit(struct Device::PerFramePtrVec *v)
	{
		per_frame_ptr_vec_clear(v);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	/* Takes ownership of an already-allocated PerFrame. */
	inline void per_frame_ptr_vec_push(struct Device::PerFramePtrVec *v, struct Device::PerFrame *p)
	{
		if (v->count >= v->cap) {
			int ncap = v->cap ? v->cap * 2 : 8;
			Device::PerFrame **nitems = (Device::PerFrame **)realloc(v->items, (size_t)ncap * sizeof(Device::PerFrame *));
			if (!nitems)
				return;
			v->items = nitems;
			v->cap = ncap;
		}
		v->items[v->count++] = p;
	}


/* ============================================================
 * atlas.hpp
 * ============================================================ */

	static const unsigned FB_WIDTH = 1024;
	static const unsigned FB_HEIGHT = 512;
	static const unsigned BLOCK_WIDTH = 8;
	static const unsigned BLOCK_HEIGHT = 8;
	static const unsigned NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH;
	static const unsigned NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT;

	enum Domain {
		Domain_Unscaled,
		Domain_Scaled
	};

	enum Stage {
		Stage_Compute,
		Stage_Transfer,
		Stage_Fragment,
		Stage_FragmentTexture
	};

	enum TextureMode {
		TextureMode_None,
		TextureMode_Palette4bpp,
		TextureMode_Palette8bpp,
		TextureMode_ABGR1555
	};

	struct Rect
	{
		unsigned x = 0;
		unsigned y = 0;
		unsigned width = 0;
		unsigned height = 0;

		Rect() = default;
		Rect(unsigned x, unsigned y, unsigned width, unsigned height)
			: x(x)
			  , y(y)
			  , width(width)
			  , height(height)
		{
		}

		inline bool operator==(const Rect &rect) const
		{
			return x == rect.x && y == rect.y && width == rect.width && height == rect.height;
		}

		inline bool operator!=(const Rect &rect) const
		{
			return x != rect.x || y != rect.y || width != rect.width || height != rect.height;
		}

		inline bool contains(const Rect &rect) const
		{
			return x <= rect.x && y <= rect.y && (x + width) >= (rect.x + rect.width) &&
				(y + height) >= (rect.y + rect.height);
		}

		inline bool intersects(const Rect &rect) const
		{
			unsigned x_end_self = x + width;
			unsigned x_end_other = rect.x + rect.width;
			unsigned y_end_self = y + height;
			unsigned y_end_other = rect.y + rect.height;
			unsigned xend = (x_end_self < x_end_other) ? x_end_self : x_end_other;
			unsigned xbegin = (x > rect.x) ? x : rect.x;
			unsigned yend = (y_end_self < y_end_other) ? y_end_self : y_end_other;
			unsigned ybegin = (y > rect.y) ? y : rect.y;
			return xbegin < xend && ybegin < yend;
		}

		inline Rect scissor(const Rect &rect) const
		{
			unsigned x_end_self = x + width;
			unsigned x_end_other = rect.x + rect.width;
			unsigned y_end_self = y + height;
			unsigned y_end_other = rect.y + rect.height;
			unsigned x0 = (x > rect.x) ? x : rect.x;
			unsigned y0 = (y > rect.y) ? y : rect.y;
			unsigned x1 = (x_end_self < x_end_other) ? x_end_self : x_end_other;
			unsigned y1 = (y_end_self < y_end_other) ? y_end_self : y_end_other;
			unsigned width = (x1 > x0) ? (x1 - x0) : 0u;
			unsigned height = (y1 > y0) ? (y1 - y0) : 0u;
			return { x0, y0, width, height };
		}

		inline void extend_bounding_box(const Rect &rect)
		{
			unsigned x_end_self = x + width;
			unsigned x_end_other = rect.x + rect.width;
			unsigned y_end_self = y + height;
			unsigned y_end_other = rect.y + rect.height;
			unsigned x0 = (x < rect.x) ? x : rect.x;
			unsigned y0 = (y < rect.y) ? y : rect.y;
			unsigned x1 = (x_end_self > x_end_other) ? x_end_self : x_end_other;
			unsigned y1 = (y_end_self > y_end_other) ? y_end_self : y_end_other;
			x = x0;
			y = y0;
			width = x1 - x0;
			height = y1 - y0;
		}
	};

	enum StatusFlag
	{
		STATUS_FB_ONLY = 0,
		STATUS_FB_PREFER = 1,
		STATUS_SFB_ONLY = 2,
		STATUS_SFB_PREFER = 3,
		STATUS_OWNERSHIP_MASK = 3,

		STATUS_COMPUTE_FB_READ = 1 << 2,
		STATUS_COMPUTE_FB_WRITE = 1 << 3,
		STATUS_COMPUTE_SFB_READ = 1 << 4,
		STATUS_COMPUTE_SFB_WRITE = 1 << 5,

		STATUS_TRANSFER_FB_READ = 1 << 6,
		STATUS_TRANSFER_SFB_READ = 1 << 7,
		STATUS_TRANSFER_FB_WRITE = 1 << 8,
		STATUS_TRANSFER_SFB_WRITE = 1 << 9,

		STATUS_FRAGMENT_SFB_READ = 1 << 10,
		STATUS_FRAGMENT_SFB_WRITE = 1 << 11,
		STATUS_FRAGMENT_FB_READ = 1 << 12,
		STATUS_FRAGMENT_FB_WRITE = 1 << 13,

		// A special stage to allow fragment to detect when it's causing a feedback loop with texture read -> fragment write.
		// This flag is added in combination with FRAGMENT_FB_READ.
		STATUS_TEXTURE_READ = 1 << 14,

		// For determining if a texture read is from a loaded image or previous rendered content
		STATUS_TEXTURE_RENDERED = 1 << 15,

		STATUS_FB_READ = STATUS_COMPUTE_FB_READ | STATUS_TRANSFER_FB_READ | STATUS_FRAGMENT_FB_READ,
		STATUS_FB_WRITE = STATUS_COMPUTE_FB_WRITE | STATUS_TRANSFER_FB_WRITE | STATUS_FRAGMENT_FB_WRITE,
		STATUS_SFB_READ = STATUS_COMPUTE_SFB_READ | STATUS_TRANSFER_SFB_READ | STATUS_FRAGMENT_SFB_READ,
		STATUS_SFB_WRITE = STATUS_COMPUTE_SFB_WRITE | STATUS_TRANSFER_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE
	};
	using StatusFlags = uint16_t;

	class Renderer;

	/* VRAM framebuffer atlas / hazard tracker. Formerly a class whose only
	 * C++-ism was a constructor that filled fb_info[] (relying on NSDMIs for the
	 * listener pointer and the renderpass sub-struct); now a plain struct driven
	 * by fbatlas_init. Renderer embeds one by value and calls fbatlas_init at the
	 * top of its constructor. All methods stay as struct methods. */
	struct FBAtlas
	{
		StatusFlags fb_info[NUM_BLOCKS_X * NUM_BLOCKS_Y];
		Renderer *listener;

		struct RenderPassState
		{
			Rect rect;
			Rect scissor;
			Rect texture_window;
			unsigned texture_offset_x, texture_offset_y;
			unsigned palette_offset_x, palette_offset_y;
			TextureMode texture_mode;
			bool inside;
		} renderpass;
	};

	/* FBAtlas free functions (converted from struct methods). The hazard atlas is
	 * embedded by value in Renderer and driven via fbatlas_*; the read/write/sync
	 * helpers and the render-pass bookkeeping all take FBAtlas *self. info()
	 * returned a StatusFlags& and now returns a StatusFlags* (its const overload
	 * folds into the same accessor). */
	static void fbatlas_read_fragment(FBAtlas *self, Domain domain, const Rect &rect);
	static Domain fbatlas_blit_vram(FBAtlas *self, const Rect &dst, const Rect &src);
	static void fbatlas_load_image(FBAtlas *self, const Rect &rect);
	static bool fbatlas_texture_rendered(FBAtlas *self, const Rect &rect);
	static void fbatlas_write_fragment(FBAtlas *self, Domain domain, const Rect &rect);
	static void fbatlas_clear_rect(FBAtlas *self, const Rect &rect, uint32_t color);
	static void fbatlas_pipeline_barrier(FBAtlas *self, StatusFlags domains);
	static void fbatlas_notify_external_barrier(FBAtlas *self, StatusFlags domains);
	static void fbatlas_flush_render_pass(FBAtlas *self);
	static void fbatlas_read_domain(FBAtlas *self, Domain domain, Stage stage, const Rect &rect);
	static bool fbatlas_write_domain(FBAtlas *self, Domain domain, Stage stage, const Rect &rect);
	static void fbatlas_sync_domain(FBAtlas *self, Domain domain, const Rect &rect);
	static void fbatlas_read_texture(FBAtlas *self, Domain domain);
	static Domain fbatlas_find_suitable_domain(FBAtlas *self, const Rect &rect);
	static void fbatlas_extend_render_pass(FBAtlas *self, const Rect &rect, bool scissor);
	static void fbatlas_discard_render_pass(FBAtlas *self);
	static bool fbatlas_inside_render_pass(FBAtlas *self, const Rect &rect);

	static inline void fbatlas_set_hazard_listener(FBAtlas *self, Renderer *hazard)
	{
		self->listener = hazard;
	}
	static inline void fbatlas_read_compute(FBAtlas *self, Domain domain, const Rect &rect)
	{
		fbatlas_sync_domain(self, domain, rect);
		fbatlas_read_domain(self, domain, Stage_Compute, rect);
	}
	static inline void fbatlas_write_compute(FBAtlas *self, Domain domain, const Rect &rect)
	{
		fbatlas_sync_domain(self, domain, rect);
		fbatlas_write_domain(self, domain, Stage_Compute, rect);
	}
	static inline void fbatlas_read_transfer(FBAtlas *self, Domain domain, const Rect &rect)
	{
		fbatlas_sync_domain(self, domain, rect);
		fbatlas_read_domain(self, domain, Stage_Transfer, rect);
	}
	static inline void fbatlas_write_transfer(FBAtlas *self, Domain domain, const Rect &rect)
	{
		fbatlas_sync_domain(self, domain, rect);
		fbatlas_write_domain(self, domain, Stage_Transfer, rect);
	}
	static inline void fbatlas_set_draw_rect(FBAtlas *self, const Rect &rect)
	{
		self->renderpass.scissor = rect;
	}
	static inline void fbatlas_set_texture_window(FBAtlas *self, const Rect &rect)
	{
		self->renderpass.texture_window = rect;
	}
	static inline TextureMode fbatlas_set_texture_mode(FBAtlas *self, TextureMode mode)
	{
		TextureMode old = self->renderpass.texture_mode;
		self->renderpass.texture_mode = mode;
		return old;
	}
	static inline void fbatlas_set_texture_offset(FBAtlas *self, unsigned x, unsigned y)
	{
		self->renderpass.texture_offset_x = x;
		self->renderpass.texture_offset_y = y;
	}
	static inline void fbatlas_set_palette_offset(FBAtlas *self, unsigned x, unsigned y)
	{
		self->renderpass.palette_offset_x = x;
		self->renderpass.palette_offset_y = y;
	}
	static inline StatusFlags *fbatlas_info(FBAtlas *self, unsigned block_x, unsigned block_y)
	{
		block_x &= NUM_BLOCKS_X - 1;
		block_y &= NUM_BLOCKS_Y - 1;
		return &self->fb_info[NUM_BLOCKS_X * block_y + block_x];
	}

	static void fbatlas_init(FBAtlas *a)
	{
		unsigned i;
		for (i = 0; i < NUM_BLOCKS_X * NUM_BLOCKS_Y; i++)
			a->fb_info[i] = STATUS_FB_PREFER;
		a->listener = NULL;
		/* renderpass: Rect is non-trivial (user ctor/operators), so memset would be
		 * UB (-Wclass-memaccess). Zero each field explicitly. This matches the
		 * former NSDMIs: the three Rects become {0,0,0,0}, the offsets 0,
		 * texture_mode TextureMode_None (value 0) and inside false. */
		a->renderpass.rect.x = 0; a->renderpass.rect.y = 0; a->renderpass.rect.width = 0; a->renderpass.rect.height = 0;
		a->renderpass.scissor.x = 0; a->renderpass.scissor.y = 0; a->renderpass.scissor.width = 0; a->renderpass.scissor.height = 0;
		a->renderpass.texture_window.x = 0; a->renderpass.texture_window.y = 0; a->renderpass.texture_window.width = 0; a->renderpass.texture_window.height = 0;
		a->renderpass.texture_offset_x = 0; a->renderpass.texture_offset_y = 0;
		a->renderpass.palette_offset_x = 0; a->renderpass.palette_offset_y = 0;
		a->renderpass.texture_mode = TextureMode_None;
		a->renderpass.inside = false;
	}

	/* POD match rule from dump.cfg: a value of -1 means "wildcard" (matches
	 * any) for that field. Was a class with a ctor + NSDMI + matches() method. */
	struct RectMatch {
		int x;
		int y;
		int w;
		int h;
	};

	static inline bool rect_match_matches(const RectMatch *m, Rect r) {
		return (m->x == -1 || m->x == r.x) && (m->y == -1 || m->y == r.y) &&
			(m->w == -1 || m->w == (int)r.width) && (m->h == -1 || m->h == (int)r.height);
	}

/* Maximum number of "ignore" rules read from dump.cfg. A fixed cap keeps the
 * list a plain inline array (no heap / no destructor); real dump configs have
 * a handful of entries, so this is never approached. */
#define DUMP_IGNORE_MAX 256

/* ============================================================
 * texture_tracker.hpp
 * ============================================================ */

extern retro_log_printf_t log_cb;

//#define VERBOSE_TEXTURE_TRACKING

/* Texture-tracker logging is debug-only: it compiles to real log_cb calls
 * in DEBUG builds (the build system passes -DDEBUG when DEBUG=1) and to a
 * no-op everywhere else, so release cores carry no logging overhead.
 *
 * The no-op variants must still consume their arguments at the syntactic
 * level, otherwise locals only used in a log statement trip
 * -Wunused-but-set-variable.  The "(void)0," prefix lets sizeof accept a
 * 1-arg invocation through the comma operator; sizeof itself is unevaluated,
 * so each arg is read by the type system without generating runtime code. */
#ifdef DEBUG
#define TT_LOG(...) log_cb(__VA_ARGS__)
#else
#define TT_LOG(...) ((void)sizeof(((void)0, __VA_ARGS__)))
#endif

#if defined(DEBUG) && defined(VERBOSE_TEXTURE_TRACKING)
#define TT_LOG_VERBOSE(...) TT_LOG(__VA_ARGS__)
#else
#define TT_LOG_VERBOSE(...) ((void)sizeof(((void)0, __VA_ARGS__)))
#endif

#include <math.h>

	struct HdTextureId {
		uint32_t hash;
		uint32_t palette_hash;

		bool operator>(const HdTextureId &other) const
		{
			if (hash != other.hash)
				return hash > other.hash;
			return palette_hash > other.palette_hash;
		}
		bool operator<(const HdTextureId &other) const
		{
			if (hash != other.hash)
				return hash < other.hash;
			return palette_hash < other.palette_hash;
		}
	};

	typedef int RectIndex; // I wanted a newtype but it's too much work in C++, so maybe TODO that later
	struct HdTextureHandle {
		RectIndex index;
		uint32_t palette_hash;
		bool fused;

		bool operator==(const HdTextureHandle &other) const
		{
			return index == other.index && palette_hash == other.palette_hash && fused == other.fused;
		}

		bool operator!=(const HdTextureHandle &other) const
		{
			return !(*this == other);
		}

		bool operator>(const HdTextureHandle &other) const
		{
			if (index != other.index)
				return index > other.index;
			if (palette_hash != other.palette_hash)
				return palette_hash > other.palette_hash;
			return fused > other.fused;
		}

		static HdTextureHandle make(RectIndex index, uint32_t palette_hash) {
			return HdTextureHandle(index, palette_hash, false);
		}
		static HdTextureHandle make_fused(RectIndex index) {
			return HdTextureHandle(index, 0, true);
		}
		static HdTextureHandle make_none() {
			return HdTextureHandle::make(-1, 0);
		}

		private:
		HdTextureHandle(RectIndex index, uint32_t palette_hash, bool fused)
			: index(index), palette_hash(palette_hash), fused(fused)
		{

		}
	};

	struct SRect {
		int x;
		int y;
		int width;
		int height;
		// Default-constructed SRect zero-initializes all fields. The result is in
		// an "invalid" state by the 4-arg constructor's invariant (width == 0 and
		// height == 0 would fail its width > 0 / height > 0 check) and is intended
		// only as a placeholder — array slot or struct field — to be overwritten
		// before being read. The 4-arg constructor below is the validated path;
		// use it for any SRect that is meant to be immediately usable.
		SRect() : x(0), y(0), width(0), height(0) {}
		SRect(int x, int y, int width, int height):
			x(x), y(y), width(width), height(height) {
				if (width <= 0 || height <= 0) {
					printf("Illegally sized SRect: %d, %d\n", width, height);
					exit(1);
				}
			}
		inline int left() {
			return x;
		}
		inline int right() {
			return x + width;
		}
		inline int top() {
			return y;
		}
		inline int bottom() {
			return y + height;
		}

		inline bool operator==(const SRect &other) const
		{
			return x == other.x && y == other.y && width == other.width && height == other.height;
		}
		inline bool operator!=(const SRect &other) const
		{
			return !(*this == other);
		}
	};

	struct HdTexture {
		SRect vram_rect;
		SRect texel_rect; // hd texels
		ImageHandle texture;
	};

	struct DumpedMode {
		TextureMode mode;
		uint32_t palette_hash;

		inline bool operator==(const DumpedMode &other) const
		{
			return mode == other.mode && palette_hash == other.palette_hash;
		}
	};

	struct UsedMode {
		TextureMode mode;
		unsigned int palette_offset_x;
		unsigned int palette_offset_y;

		inline bool operator==(const UsedMode &other) const
		{
			return mode == other.mode && palette_offset_x == other.palette_offset_x && palette_offset_y == other.palette_offset_y;
		}
	};

	/* ------------------------------------------------------------------------- *
	 * HdTexMap - palette_hash -> (Vulkan image, alpha flags), MSVC C89.
	 *
	 * Replaces std::map<uint32_t, HdImageHandle> on TextureUpload. Backed by a
	 * sorted, malloc'd array of POD entries keyed by palette hash (binary-search
	 * lookup, insert keeps it sorted). The image is held as a raw Image*
	 * (so the array can realloc) with the intrusive refcount managed by hand: a
	 * reference is taken when an entry is stored and released on
	 * replace/erase/clear/free - matching the raw-pointer scheme already used by
	 * the GPU image cache. Entries are POD, so TextureUpload can be a plain
	 * malloc-backed value once this and the other members are converted.
	 * ------------------------------------------------------------------------- */
	struct HdTexEntry {
		uint32_t       key;          /* palette hash */
		Image *image;        /* owns one reference while stored */
		int            alpha_flags;
	};
	struct HdTexMap {
		HdTexEntry *entries;
		int         count;
		int         cap;
	};

	static void hd_tex_map_init(HdTexMap *m)
	{
		m->entries = NULL;
		m->count = 0;
		m->cap = 0;
	}
	static int hd_tex_map_lower_bound(const HdTexMap *m, uint32_t key)
	{
		int lo = 0, hi = m->count;
		while (lo < hi) {
			int mid = lo + ((hi - lo) >> 1);
			if (m->entries[mid].key < key)
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	}
	/* Returns the entry for key, or NULL. */
	static HdTexEntry *hd_tex_map_find(HdTexMap *m, uint32_t key)
	{
		int i = hd_tex_map_lower_bound(m, key);
		if (i < m->count && m->entries[i].key == key)
			return &m->entries[i];
		return NULL;
	}
	static int hd_tex_map_contains(const HdTexMap *m, uint32_t key)
	{
		int i = hd_tex_map_lower_bound(m, key);
		return i < m->count && m->entries[i].key == key;
	}
	/* Insert or replace key -> (image, alpha). Takes a reference on `image`;
	 * releases any image previously stored at this key. */
	static void hd_tex_map_set(HdTexMap *m, uint32_t key, Image *image, int alpha_flags)
	{
		int i = hd_tex_map_lower_bound(m, key);
		if (i < m->count && m->entries[i].key == key) {
			if (image) image_add_reference(image);
			if (m->entries[i].image) image_release_reference(m->entries[i].image);
			m->entries[i].image = image;
			m->entries[i].alpha_flags = alpha_flags;
			return;
		}
		if (m->count == m->cap) {
			int ncap = m->cap ? m->cap * 2 : 8;
			HdTexEntry *ne = (HdTexEntry *)realloc(m->entries, (size_t)ncap * sizeof(HdTexEntry));
			if (!ne)
				return;
			m->entries = ne;
			m->cap = ncap;
		}
		memmove(&m->entries[i + 1], &m->entries[i], (size_t)(m->count - i) * sizeof(HdTexEntry));
		if (image) image_add_reference(image);
		m->entries[i].key = key;
		m->entries[i].image = image;
		m->entries[i].alpha_flags = alpha_flags;
		m->count++;
	}
	/* Release all image refs and reset to empty (keeps allocation). */
	static void hd_tex_map_clear(HdTexMap *m)
	{
		int i;
		for (i = 0; i < m->count; i++)
			if (m->entries[i].image)
				image_release_reference(m->entries[i].image);
		m->count = 0;
	}
	static void hd_tex_map_free(HdTexMap *m)
	{
		hd_tex_map_clear(m);
		free(m->entries);
		m->entries = NULL;
		m->cap = 0;
	}
	/* Deep-copy src into dst (dst assumed empty/inited); re-acquires image refs. */
	static void hd_tex_map_copy(HdTexMap *dst, const HdTexMap *src)
	{
		int i;
		dst->entries = NULL;
		dst->count = src->count;
		dst->cap = src->count;
		if (src->count) {
			dst->entries = (HdTexEntry *)malloc((size_t)src->count * sizeof(HdTexEntry));
			for (i = 0; i < src->count; i++) {
				dst->entries[i] = src->entries[i];
				if (dst->entries[i].image)
					image_add_reference(dst->entries[i].image);
			}
		}
	}

	struct TextureUpload {
		/* Intrusive refcount for shared ownership across TextureRects (replaces
		 * std::shared_ptr<TextureUpload>). A freshly constructed upload starts at
		 * 0; texture_upload_new() bumps it to 1. Copies (deep-copy ctor/assignment,
		 * used by the by-value save-state map) get their OWN fresh count - the
		 * refcount is deliberately not copied. */
		int refcount;
		/* VRAM source pixels (owned). Was std::vector<uint16_t>; filled once at
		 * creation and thereafter read-only. */
		uint16_t *image;
		int       image_count;
		bool dumpable;
		int width;
		int height;
		uint32_t hash;
		/* Modes already dumped to disk (owned growable array, append-only).
		 * Was std::vector<DumpedMode>. */
		DumpedMode *dumped_modes;
		int         dumped_modes_count;
		int         dumped_modes_cap;
		HdTexMap textures; // palette hash -> (image, alpha)
				   // (HD load bookkeeping lives on the TextureTracker: hd_gpu_cache / hd_cache /
				   //  requested / pending_attach, keyed by (hash,palette) so it survives this
				   //  upload being recreated as the sprite animation churns VRAM.)
	};

	/* TextureUpload owns malloc'd buffers (image, dumped_modes) and an HdTexMap,
	 * but is a plain struct with no C++ special members: it is created, copied
	 * and destroyed only through the explicit helpers below. texture_upload_init
	 * mirrors the former default constructor, texture_upload_destroy the former
	 * destructor, and texture_upload_copy_contents the former deep copy (a fresh
	 * refcount is NOT taken from the source - the destination keeps its own). */
	static void texture_upload_init(TextureUpload *u)
	{
		u->refcount = 0;
		u->image = NULL;
		u->image_count = 0;
		u->dumpable = false;
		u->width = 0;
		u->height = 0;
		u->hash = 0;
		u->dumped_modes = NULL;
		u->dumped_modes_count = 0;
		u->dumped_modes_cap = 0;
		hd_tex_map_init(&u->textures);
	}
	static void texture_upload_destroy(TextureUpload *u)
	{
		free(u->image);
		free(u->dumped_modes);
		hd_tex_map_free(&u->textures);
		free(u);
	}
	/* Deep-copy the owned contents of src into dst, releasing dst's existing
	 * buffers/texmap first. dst->refcount is left untouched (matching the old
	 * copy-assignment, which never copied the refcount). */
	static void texture_upload_copy_contents(TextureUpload *dst, const TextureUpload *src)
	{
		if (dst == src)
			return;
		free(dst->image);
		free(dst->dumped_modes);
		hd_tex_map_free(&dst->textures);
		dst->image = NULL;
		dst->dumped_modes = NULL;
		dst->image_count = src->image_count;
		dst->dumped_modes_count = src->dumped_modes_count;
		dst->dumped_modes_cap = src->dumped_modes_count;
		dst->dumpable = src->dumpable;
		dst->width = src->width;
		dst->height = src->height;
		dst->hash = src->hash;
		if (dst->image_count) {
			dst->image = (uint16_t *)malloc((size_t)dst->image_count * sizeof(uint16_t));
			memcpy(dst->image, src->image, (size_t)dst->image_count * sizeof(uint16_t));
		}
		if (dst->dumped_modes_count) {
			dst->dumped_modes = (DumpedMode *)malloc((size_t)dst->dumped_modes_count * sizeof(DumpedMode));
			memcpy(dst->dumped_modes, src->dumped_modes, (size_t)dst->dumped_modes_count * sizeof(DumpedMode));
		}
		hd_tex_map_copy(&dst->textures, &src->textures);
	}

	/* Allocate a new TextureUpload with refcount 1 (the caller owns that ref). */
	static TextureUpload *texture_upload_new()
	{
		TextureUpload *u = (TextureUpload *)malloc(sizeof(TextureUpload));
		texture_upload_init(u);
		u->refcount = 1;
		return u;
	}
	static void texture_upload_acquire(TextureUpload *u)
	{
		if (u)
			u->refcount++;
	}
	static void texture_upload_release(TextureUpload *u)
	{
		if (u && --u->refcount == 0)
			texture_upload_destroy(u); /* frees image/dumped_modes and releases the texmap refs */
	}

	// byte buffer plus its size, rather than std::vector<uint8_t>. This is a POD
	// (trivially copyable) struct - copying it copies the pointer, NOT the bytes,
	// so ownership is by convention: exactly one LoadedLevels owns each buffer and
	// frees it. Ownership transfers are explicit pointer-steals (push_move /
	// move-assign helpers below); there is no implicit deep copy and no
	// destructor. This keeps it storable in plain arrays and realloc-movable.
	struct LoadedImage {
		uint8_t *owned_data; // RGBA, owned_size bytes (NULL if empty)
		size_t   owned_size;
		int width;
		int height;
	};

	// Allocate the RGBA buffer for a level (width*height*4). Frees any prior
	// buffer. Returns 0 on success, -1 on allocation failure (buffer left NULL).
	static inline int loaded_image_alloc(LoadedImage *img, int width, int height)
	{
		size_t bytes = (size_t)width * (size_t)height * 4u;
		free(img->owned_data);
		img->owned_data = (uint8_t *)malloc(bytes ? bytes : 1);
		img->owned_size = img->owned_data ? bytes : 0;
		img->width  = width;
		img->height = height;
		return img->owned_data ? 0 : -1;
	}

	// Zero-initialise a level (no buffer). Use before loaded_image_alloc.
	static inline void loaded_image_init(LoadedImage *img)
	{
		img->owned_data = NULL;
		img->owned_size = 0;
		img->width      = 0;
		img->height     = 0;
	}

	// A decoded texture as a set of mip levels. C-style dynamic array: `levels`
	// is a malloc'd array of `count` LoadedImage, owning all buffers. Replaces
	// std::vector<LoadedImage>. POD/trivially-copyable: a copy aliases the same
	// buffers, so callers move ownership explicitly (loaded_levels_move) and free
	// explicitly (loaded_levels_reset). Initialise with loaded_levels_init or
	// zero-init before first use.
	struct LoadedLevels {
		LoadedImage *levels;
		int          count;
	};

	/* Total owned bytes across all levels. */
	static size_t loaded_levels_byte_size(const LoadedLevels *l)
	{
		size_t b = 0;
		int i;
		for (i = 0; i < l->count; i++)
			b += l->levels[i].owned_size;
		return b;
	}

	/* Append by stealing *src's buffer (src left empty). Grows with realloc.
	 * Returns the stored level, or NULL on alloc failure. */
	static LoadedImage *loaded_levels_push_move(LoadedLevels *l, LoadedImage *src)
	{
		LoadedImage *grown = (LoadedImage *)realloc(l->levels, (size_t)(l->count + 1) * sizeof(LoadedImage));
		if (!grown)
			return NULL;
		l->levels = grown;
		l->levels[l->count] = *src; /* POD: copies the pointer (steal) */
		loaded_image_init(src);
		return &l->levels[l->count++];
	}

	/* Free all buffers + the array, return to empty state. */
	static void loaded_levels_reset(LoadedLevels *l)
	{
		int i;
		for (i = 0; i < l->count; i++)
			free(l->levels[i].owned_data);
		free(l->levels);
		l->levels = NULL;
		l->count  = 0;
	}

	// Zero-initialise (no allocation).
	static inline void loaded_levels_init(LoadedLevels *l)
	{
		l->levels = NULL;
		l->count  = 0;
	}

	// Move ownership src -> dst (dst's prior contents freed; src left empty).
	static inline void loaded_levels_move(LoadedLevels *dst, LoadedLevels *src)
	{
		if (dst == src)
			return;
		loaded_levels_reset(dst);
		dst->levels = src->levels;
		dst->count  = src->count;
		src->levels = NULL;
		src->count  = 0;
	}

	class Renderer;

	/* Max length for HD texture file paths built by the path helpers below.
	 * Defined here so both IORequest and the path helpers can use it. */
	enum { PATH_MAX_TT = 4096 + 256 };

	enum IORequestKind {
		IORequestKind_Load,
		IORequestKind_Dump,
	};

	struct IORequest {
		struct IORequest *next;        /* intrusive FIFO link (queue-owned) */
		IORequestKind kind;
		// Load payload (valid when kind == Load):
		uint32_t hash;
		uint32_t palette_hash;
		// Dump payload (valid when kind == Dump):
		char     path[PATH_MAX_TT];
		int      width;
		int      height;
		uint8_t *bytes;                /* owned RGBA bytes (NULL if none) */
		size_t   bytes_len;
	};

	static void io_request_free(IORequest *r)
	{
		if (r) {
			free(r->bytes);
			free(r);
		}
	}

	const int ALPHA_FLAG_OPAQUE = 1;
	const int ALPHA_FLAG_SEMI_TRANSPARENT = 2;
	const int ALPHA_FLAG_TRANSPARENT = 4;

	struct IOResponse {
		struct IOResponse *next;       /* intrusive FIFO link (queue-owned) */
		uint32_t hash;
		uint32_t palette_hash;
		int alpha_flags;
		LoadedLevels levels;
	};

	static void io_response_free(IOResponse *r)
	{
		if (r) {
			loaded_levels_reset(&r->levels);
			free(r);
		}
	}

	struct IOChannel {
		slock_t *lock;
		scond_t *cond;
		/* Intrusive FIFO lists (protected by `lock`). Heads are popped/drained,
		 * tails are where producers append. Replaces std::vector<IORequest> /
		 * std::vector<IOResponse>. */
		IORequest  *req_head,  *req_tail;
		IOResponse *resp_head, *resp_tail;
		bool done;
		/* Cross-thread refcount (replaces std::shared_ptr<IOChannel>). The owning
		 * IOThread holds one reference and each detached worker holds one; whichever
		 * releases last frees the channel. Mutated only outside the lock, at
		 * thread-spawn and thread-exit, so a plain int with no overlap is fine. */
		int refcount;
	};

	static void io_channel_destroy(IOChannel *c);
	static IOChannel *io_channel_new() {
		IOChannel *c = (IOChannel *)malloc(sizeof(IOChannel));
		c->lock = slock_new();
		c->cond = scond_new();
		c->req_head = c->req_tail = NULL;
		c->resp_head = c->resp_tail = NULL;
		c->done = false;
		c->refcount = 1;
		return c;
	}
	/* The refcount is touched from the owning thread (spawn/teardown) and from the
	 * detached workers (exit), so the increment/decrement must be serialised. A
	 * single process-wide lock guards every transition; the actual free happens
	 * after the lock is dropped so we never reference the channel's own lock once
	 * it may be gone. */
	static slock_t *io_channel_rc_lock = NULL;
	static void io_channel_rc_lock_init() {
		if (!io_channel_rc_lock)
			io_channel_rc_lock = slock_new();
	}
	static void io_channel_acquire(IOChannel *c) {
		if (!c)
			return;
		slock_lock(io_channel_rc_lock);
		c->refcount++;
		slock_unlock(io_channel_rc_lock);
	}
	static void io_channel_release(IOChannel *c) {
		bool should_free;
		if (!c)
			return;
		slock_lock(io_channel_rc_lock);
		should_free = (--c->refcount == 0);
		slock_unlock(io_channel_rc_lock);
		if (should_free)
			io_channel_destroy(c);
	}

	/* FIFO helpers (caller holds channel->lock). Defined here so the IO worker
	 * (io_thread) and the producers can all see them. */
	static void io_channel_push_request(IOChannel *c, IORequest *r) {
		r->next = NULL;
		if (c->req_tail) c->req_tail->next = r; else c->req_head = r;
		c->req_tail = r;
	}
	static IORequest *io_channel_pop_request(IOChannel *c) {
		IORequest *r = c->req_head;
		if (r) {
			c->req_head = r->next;
			if (!c->req_head) c->req_tail = NULL;
			r->next = NULL;
		}
		return r;
	}
	static void io_channel_push_response(IOChannel *c, IOResponse *r) {
		r->next = NULL;
		if (c->resp_tail) c->resp_tail->next = r; else c->resp_head = r;
		c->resp_tail = r;
	}
	/* Steal the entire response list; channel left empty. Returns the head. */
	static IOResponse *io_channel_take_responses(IOChannel *c) {
		IOResponse *head = c->resp_head;
		c->resp_head = c->resp_tail = NULL;
		return head;
	}

	/* Owns the IO worker thread pool. Formerly a class with a constructor
	 * (create the channel, spin up NUM_IO_THREADS detached workers each holding a
	 * channel reference) and destructor (signal done, wake the workers, drop this
	 * thread's reference); now a plain struct driven by io_thread_init /
	 * io_thread_deinit. The single member is the refcounted channel pointer.
	 * TextureTracker embeds one by value and drives its init/deinit. */
	struct IOThread {
		IOChannel *channel; /* refcounted; one ref held here, one per worker */
	};

	static void io_thread_init(IOThread *t);
	static void io_thread_deinit(IOThread *t);

	struct Palette {
		uint16_t *data;
		uint32_t hash;
	};

	struct CachedPaletteHash {
		Rect rect;
		uint32_t hash;
	};

	//============
	// RectTracker

	/* TextureRect is a trivially-copyable POD: a borrowed view of an upload plus a
	 * subrect. It does NOT manage the refcount in special members (so it relocates
	 * with a bitwise move - no per-copy acquire/release, no exception scaffolding in
	 * the containers that hold it). Ownership lives at container boundaries instead:
	 * a container retains on insert and releases on erase/clear/destroy via
	 * texture_rect_retain / texture_rect_release. Transient TextureRect values
	 * (subTexture results, clip pairs, scratch arrays) are borrowing and need no
	 * ref ops. */
	struct TextureRect {
		TextureUpload *upload;
		// the offset into the original upload rect (offset_x + vram_rect.width <= upload->width)
		int offset_x;
		int offset_y;
		SRect vram_rect;

		// in vram size (not hd), local to the uploaded data, different hd textures for different palettes could have different sizes anyway
		SRect texture_subrect() const {
			return SRect(offset_x, offset_y, vram_rect.width, vram_rect.height);
		}

		inline bool operator==(const TextureRect &other) const
		{
			return upload == other.upload && offset_x == other.offset_x && offset_y == other.offset_y && vram_rect == other.vram_rect;
		}
		inline bool operator!=(const TextureRect &other) const
		{
			return !(*this == other);
		}
	};

	/* Build a TextureRect (borrowing - does not take a reference). */
	static inline TextureRect make_texture_rect(TextureUpload *upload, int offset_x, int offset_y, SRect vram_rect)
	{
		TextureRect t;
		t.upload = upload;
		t.offset_x = offset_x;
		t.offset_y = offset_y;
		t.vram_rect = vram_rect;
		return t;
	}
	/* Ownership transfer helpers, used by owning containers only. */
	static inline void texture_rect_retain(const TextureRect *t)  { texture_upload_acquire(t->upload); }
	static inline void texture_rect_release(const TextureRect *t) { texture_upload_release(t->upload); }

	/* Borrowing scratch array of (POD) TextureRects - no ownership, used for the
	 * blit/clear_rect transients that are immediately re-placed into owning
	 * containers. Trivially relocatable, so push is a bare realloc/append. */
	POD_VEC_DECLARE(TextureRectVec, TextureRect);


	// TODO: better name
	struct EnduringTextureRect {
		TextureRect texture_rect;
		bool alive;
	};

	/* Owning, trivially-relocatable array of EnduringTextureRect (replaces
	 * std::vector<EnduringTextureRect>). Because TextureRect is now POD, growth is a
	 * realloc (bitwise relocation, no per-element move-ctor or exception
	 * scaffolding). Ownership is explicit: push retains the upload, and any slot
	 * that leaves the array (compaction drop, clear, free) releases it. */
	struct EnduringRectArr {
		EnduringTextureRect *a;
		int count;
		int cap;
		/* Pointer-range iteration so existing range-for loops work unchanged. */
		EnduringTextureRect *begin() { return a; }
		EnduringTextureRect *end()   { return a + count; }
		const EnduringTextureRect *begin() const { return a; }
		const EnduringTextureRect *end()   const { return a + count; }
	};
	static inline void enduring_arr_init(EnduringRectArr *v) { v->a = NULL; v->count = 0; v->cap = 0; }
	static inline void enduring_arr_push(EnduringRectArr *v, TextureRect tr, bool alive) {
		if (v->count == v->cap) {
			int ncap = v->cap ? v->cap * 2 : 16;
			EnduringTextureRect *na = (EnduringTextureRect *)realloc(v->a, (size_t)ncap * sizeof(EnduringTextureRect));
			if (!na)
				return;
			v->a = na;
			v->cap = ncap;
		}
		texture_rect_retain(&tr);            /* the array now owns a reference */
		v->a[v->count].texture_rect = tr;
		v->a[v->count].alive = alive;
		v->count++;
	}
	/* Drop !alive slots, releasing their refs; survivors relocate by bitwise move. */
	static inline void enduring_arr_compact(EnduringRectArr *v) {
		int w = 0, i;
		for (i = 0; i < v->count; i++) {
			if (v->a[i].alive) {
				if (w != i) v->a[w] = v->a[i];
				w++;
			} else {
				texture_rect_release(&v->a[i].texture_rect);
			}
		}
		v->count = w;
	}
	static inline void enduring_arr_clear(EnduringRectArr *v) {
		int i;
		for (i = 0; i < v->count; i++)
			texture_rect_release(&v->a[i].texture_rect);
		v->count = 0;
	}
	static inline void enduring_arr_free(EnduringRectArr *v) {
		enduring_arr_clear(v);
		free(v->a);
		v->a = NULL;
		v->cap = 0;
	}

	/* Owning vector of (POD) TextureRects, used where the rect list is itself
	 * value-copied/compared (FusionRects, RestorableRect). The element is trivially
	 * relocatable so the backing std::vector relocates cheaply, but ownership must
	 * be explicit: this wrapper retains on push and on copy, and releases on
	 * destroy/clear/assign. */
	struct OwnedRectVec {
		TextureRectVec v;
		OwnedRectVec() { v.items = NULL; v.count = 0; v.cap = 0; }
		~OwnedRectVec() {
			for (int i = 0; i < v.count; i++) texture_rect_release(&v.items[i]);
			TextureRectVec_free_storage(&v);
		}
		OwnedRectVec(const OwnedRectVec &o) {
			v.items = NULL; v.count = 0; v.cap = 0;
			copy_from(o.v);
			for (int i = 0; i < v.count; i++) texture_rect_retain(&v.items[i]);
		}
		OwnedRectVec &operator=(const OwnedRectVec &o) {
			if (this != &o) {
				for (int i = 0; i < v.count; i++) texture_rect_release(&v.items[i]);
				copy_from(o.v);
				for (int i = 0; i < v.count; i++) texture_rect_retain(&v.items[i]);
			}
			return *this;
		}
		void push(TextureRect t) { texture_rect_retain(&t); TextureRectVec_push(&v, &t); }
		size_t size() const { return (size_t)v.count; }
		TextureRect &operator[](size_t i) { return v.items[i]; }
		const TextureRect &operator[](size_t i) const { return v.items[i]; }
		bool operator==(const OwnedRectVec &o) const {
			if (v.count != o.v.count) return false;
			for (int i = 0; i < v.count; i++)
				if (!(v.items[i] == o.v.items[i])) return false;
			return true;
		}
		TextureRect *begin() { return v.items; }
		TextureRect *end()   { return v.items + v.count; }
		const TextureRect *begin() const { return v.items; }
		const TextureRect *end()   const { return v.items + v.count; }

	private:
		/* Deep-copy the POD elements of src into v (replacing v's storage).
		 * Refcounts are adjusted by the caller (retain after, release before). */
		void copy_from(const TextureRectVec &src) {
			v.count = 0;
			if (src.count <= 0)
				return;
			size_t n = (size_t)src.count;
			if (src.count > v.cap) {
				TextureRect *ni = (TextureRect *)realloc(v.items, n * sizeof(TextureRect));
				if (!ni)
					return;
				v.items = ni;
				v.cap = src.count;
			}
			memcpy(v.items, src.items, n * sizeof(TextureRect));
			v.count = src.count;
		}
	};

	const int LOOKUP_GRID_COLUMNS = 16;
	const int LOOKUP_GRID_ROWS = 2;
	const int LOOKUP_CELL_WIDTH = 64;
	const int LOOKUP_CELL_HEIGHT = 256;

	/* Sorted set of RectIndex (int), MSVC C89. Replaces std::unordered_set<RectIndex>
	 * for the overlap-dedup scratch set: a malloc'd sorted int array with
	 * binary-search insert (dedups), cleared and refilled each query, then iterated
	 * in order. Order differs from the old unordered_set (now ascending by index),
	 * which only affects iteration order of overlapping rects - the consumers don't
	 * depend on it. */
	struct RectIndexSet {
		RectIndex *items;
		int        count;
		int        cap;
	};
	static void rect_index_set_init(RectIndexSet *s) { s->items = NULL; s->count = 0; s->cap = 0; }
	static void rect_index_set_free(RectIndexSet *s) { free(s->items); s->items = NULL; s->count = 0; s->cap = 0; }
	static void rect_index_set_clear(RectIndexSet *s) { s->count = 0; }
	static int rect_index_set_lower_bound(const RectIndexSet *s, RectIndex v) {
		int lo = 0, hi = s->count;
		while (lo < hi) { int mid = lo + ((hi - lo) >> 1); if (s->items[mid] < v) lo = mid + 1; else hi = mid; }
		return lo;
	}
	static void rect_index_set_insert(RectIndexSet *s, RectIndex v) {
		int i = rect_index_set_lower_bound(s, v);
		if (i < s->count && s->items[i] == v) return;
		if (s->count == s->cap) {
			int ncap = s->cap ? s->cap * 2 : 16;
			RectIndex *ni = (RectIndex *)realloc(s->items, (size_t)ncap * sizeof(RectIndex));
			if (!ni)
				return;
			s->items = ni;
			s->cap = ncap;
		}
		memmove(&s->items[i + 1], &s->items[i], (size_t)(s->count - i) * sizeof(RectIndex));
		s->items[i] = v;
		s->count++;
	}

	/* Spatial lookup grid over psx texture pages. Formerly a class with a
	 * constructor/destructor managing the per-cell malloc'd arrays; now a plain
	 * struct driven by lookup_grid_init / lookup_grid_deinit. Each Cell is a
	 * growable array of POD LookupEntry; insert/get/clear are unchanged methods.
	 * RectTracker embeds one by value and drives its init/deinit. */
	struct LookupGrid {
		struct LookupEntry {
			SRect rect;
			RectIndex index;
		};
		/* Each cell is a psx texture page (64x256); was std::vector<LookupEntry>.
		 * Now a malloc'd growable array per cell (POD entries). */
		struct Cell {
			LookupEntry *entries;
			int count;
			int cap;
		};
		Cell cells[LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS];
	};

	static void lookup_grid_init(LookupGrid *g);
	static void lookup_grid_deinit(LookupGrid *g);
	static void lookup_grid_insert(LookupGrid *self, SRect r, RectIndex index);
	static void lookup_grid_get(LookupGrid *self, SRect r, RectIndexSet *results);
	static void lookup_grid_clear(LookupGrid *self);

	/* RectTracker: tracks placed/uploaded texture rects with a spatial lookup grid.
	 * Converted from a C++ class to a plain C struct + rect_tracker_* free
	 * functions. The ctor/dtor (which init/free the EnduringRectArr and LookupGrid)
	 * become rect_tracker_init / rect_tracker_deinit, called by the embedding
	 * TextureTracker's constructor/destructor. */
	struct RectTracker {
		EnduringRectArr textures; // owning trivially-relocatable array
		LookupGrid lookup_grid;
		bool lookup_grid_dirty;
	};

	static inline void rect_tracker_init(struct RectTracker *self)
	{
		enduring_arr_init(&self->textures);
		lookup_grid_init(&self->lookup_grid);
		self->lookup_grid_dirty = false;
	}
	static inline void rect_tracker_deinit(struct RectTracker *self)
	{
		enduring_arr_free(&self->textures);
		lookup_grid_deinit(&self->lookup_grid);
	}

	void rect_tracker_place(struct RectTracker *self, TextureRect texture);
	void rect_tracker_upload(struct RectTracker *self, SRect rect, TextureUpload *upload);
	void rect_tracker_blit(struct RectTracker *self, SRect dst, SRect src);
	static void rect_tracker_clear_rect(struct RectTracker *self, SRect *rect);
	static inline void rect_tracker_clear(struct RectTracker *self, SRect rect)
	{
		rect_tracker_clear_rect(self, &rect);
		self->lookup_grid_dirty = true;
	}
	void rect_tracker_releaseDeadHandles(struct RectTracker *self);
	/* Returns results by pointer (was a reference). */
	RectIndexSet *rect_tracker_overlapping(struct RectTracker *self, Rect rect, RectIndexSet *results);

	/* This pointer will be valid until the next upload/blit/clear/endFrame, so use
	 * it immediately. Returns NULL when index is out of range. */
	TextureRect *rect_tracker_get_index(struct RectTracker *self, RectIndex index);

	/* Returns NULL if no texture with the given hash can be found. */
	TextureUpload *rect_tracker_find_upload(struct RectTracker *self, uint32_t hash);

	static void rect_tracker_rebuild_lookup_grid(struct RectTracker *self);
	// RectTracker
	//============

	struct FusionRects {
		OwnedRectVec rects;
		Rect vram_rect;
		unsigned int scaleX = 0;
		unsigned int scaleY = 0;

		bool operator==(const FusionRects &other) const {
			return vram_rect == other.vram_rect && scaleX == other.scaleX && scaleY == other.scaleY && rects == other.rects;
		}

		bool operator!=(const FusionRects &other) const {
			return !(*this == other);
		}
	};

	struct FusedPage {
		ImageHandle texture;

		uint32_t palette;
		Rect full_page_rect;

		bool dirty = false;
		bool dead = false;

		FusionRects fusion;
	};

	/* Copy/destroy helpers for FusedPage's ImageHandle member, replacing the
	 * implicit copy-ctor incref / destructor decref the macro used to provide.
	 * The remaining fields are trivially copyable. */
	static inline void fp_copy(FusedPage *dst, const FusedPage *src) {
		*dst = *src;                 /* bitwise copy of all fields */
		dst->texture.data = src->texture.data;
		if (dst->texture.data) image_add_reference(dst->texture.data);  /* retain */
	}
	/* Seed a raw (uninitialised) FusedPage slot's only heap-owning C++ member --
	 * fusion.rects (an OwnedRectVec) -- to the empty state, so the subsequent
	 * fp_copy's *dst=*src (which runs OwnedRectVec::operator=) does not release
	 * through the slot's indeterminate items/count. Required before fp_copy into
	 * malloc'd storage (grow/push); the in-place compaction path already has a
	 * live dst and must NOT use this. */
	static inline void fp_init_raw(FusedPage *p) {
		p->fusion.rects.v.items = NULL;
		p->fusion.rects.v.count = 0;
		p->fusion.rects.v.cap = 0;
	}
	static inline void fp_destroy(FusedPage *p) {
		ih_reset(&p->texture);
	}

	/* Owning array of FusedPage. Replaces std::vector<FusedPage>. FusedPage owns
	 * an ImageHandle and a FusionRects (whose OwnedRectVec retains/releases its
	 * TextureRects), and is used by copy (push_back(page) and the compaction's
	 * element copy-assign), so this container copy-constructs/-assigns elements
	 * rather than moving: growth copy-constructs each element into new storage
	 * with placement new and destroys the old slot. push() copy-inserts;
	 * indexed access and pointer-range iteration keep the existing size()/
	 * operator[]/range-for uses. truncate(n) destroys the tail [n, count) and is
	 * how remove_dead() drops compacted-out entries (replacing erase(it, end())).
	 * Move-only at the container level. */
	/* FusedPageVec: owning growable array of FusedPage (each holds a refcounted
	 * ImageHandle, so growth/teardown go through fp_copy/fp_destroy). Converted from
	 * a move-only C++ container to a plain C struct + fused_page_vec_* free
	 * functions; the embedding FusedPages drives init_empty/deinit. */
	struct FusedPageVec {
		FusedPage *items;
		int count;
		int cap;
	};

	static void fused_page_vec_grow(struct FusedPageVec *v, int ncap)
	{
		FusedPage *nitems = (FusedPage *)malloc((size_t)ncap * sizeof(FusedPage));
		int i;
		for (i = 0; i < v->count; i++) {
			fp_init_raw(&nitems[i]);
			fp_copy(&nitems[i], &v->items[i]);
			fp_destroy(&v->items[i]);
		}
		free(v->items);
		v->items = nitems;
		v->cap = ncap;
	}
	static inline void fused_page_vec_init_empty(struct FusedPageVec *v)
	{
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	static inline void fused_page_vec_deinit(struct FusedPageVec *v)
	{
		int i;
		for (i = 0; i < v->count; i++)
			fp_destroy(&v->items[i]);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}
	static inline void fused_page_vec_push(struct FusedPageVec *v, const FusedPage *e)
	{
		if (v->count >= v->cap)
			fused_page_vec_grow(v, v->cap ? v->cap * 2 : 8);
		fp_init_raw(&v->items[v->count]);
		fp_copy(&v->items[v->count], e);
		v->count++;
	}
	static inline int fused_page_vec_size(const struct FusedPageVec *v) { return v->count; }
	static inline FusedPage *fused_page_vec_at(struct FusedPageVec *v, int i) { return &v->items[i]; }
	/* Destroy elements [n, count); used to drop the compacted-out tail. */
	static inline void fused_page_vec_truncate(struct FusedPageVec *v, int n)
	{
		int i;
		for (i = n; i < v->count; i++)
			fp_destroy(&v->items[i]);
		v->count = n;
	}

	struct FusedPages {
		FusedPageVec pages;
	};

	static inline void fused_pages_init(struct FusedPages *self) { fused_page_vec_init_empty(&self->pages); }
	static inline void fused_pages_deinit(struct FusedPages *self) { fused_page_vec_deinit(&self->pages); }
	HdTextureHandle fused_pages_get_or_make(struct FusedPages *self, Rect page_rect, uint32_t palette, struct RectTracker *tracker, Renderer *uploader);
	HdTexture fused_pages_get_from_handle(struct FusedPages *self, HdTextureHandle handle, ImageHandle *default_hd_texture);
	void fused_pages_mark_dirty(struct FusedPages *self, Rect rect); // For blit dst, upload, and hd texture load
	void fused_pages_mark_dead(struct FusedPages *self, Rect rect); // For clear
	void fused_pages_rebuild_dirty(struct FusedPages *self, struct RectTracker *tracker, Renderer *uploader);
	void fused_pages_remove_dead(struct FusedPages *self);
	void fused_pages_dbg_print_info(struct FusedPages *self);

	struct RestorableRect {
		Rect rect;
		uint32_t hash;
		OwnedRectVec to_restore;
	};

	/* Owning array of RestorableRect. Replaces std::vector<RestorableRect>.
	 * RestorableRect holds an OwnedRectVec (retain/release on copy, move
	 * suppressed), so it is copyable but not movable - the same way std::vector
	 * relocated it by copy. This container therefore copy-constructs/-assigns
	 * elements: growth copy-constructs each into new storage with placement new
	 * and destroys the old slot; push() copy-inserts (matching the old
	 * push_back(std::move(rr)), which - move being suppressed - was a copy);
	 * clear()/the destructor run each element's destructor (releasing its rect
	 * refs). erase_at(i) removes one element by copy-assigning the tail down and
	 * destroying the now-duplicated last slot, matching std::vector::erase. */
	struct RestorableRectVec {
		RestorableRect *items;
		int count;
		int cap;

		RestorableRectVec() : items(NULL), count(0), cap(0) {}
		~RestorableRectVec() { destroy(); }
		RestorableRectVec(RestorableRectVec &&o) noexcept
			: items(o.items), count(o.count), cap(o.cap) { o.items = NULL; o.count = 0; o.cap = 0; }
		RestorableRectVec &operator=(RestorableRectVec &&o) noexcept {
			if (this != &o) {
				destroy();
				items = o.items; count = o.count; cap = o.cap;
				o.items = NULL; o.count = 0; o.cap = 0;
			}
			return *this;
		}
		RestorableRectVec(const RestorableRectVec &) = delete;
		RestorableRectVec &operator=(const RestorableRectVec &) = delete;

		void push(const RestorableRect &v) {
			if (count >= cap)
				grow(cap ? cap * 2 : 8);
			new (&items[count]) RestorableRect(v);
			count++;
		}
		int size() const { return count; }
		bool empty() const { return count == 0; }
		RestorableRect &operator[](int i) { return items[i]; }
		const RestorableRect &operator[](int i) const { return items[i]; }
		RestorableRect *begin() { return items; }
		RestorableRect *end() { return items + count; }
		const RestorableRect *begin() const { return items; }
		const RestorableRect *end() const { return items + count; }
		void clear() {
			for (int i = 0; i < count; i++)
				items[i].~RestorableRect();
			count = 0;
		}
		void erase_at(int i) {
			for (int j = i; j + 1 < count; j++)
				items[j] = items[j + 1];
			items[count - 1].~RestorableRect();
			count--;
		}

	private:
		void grow(int ncap) {
			RestorableRect *nitems = (RestorableRect *)malloc((size_t)ncap * sizeof(RestorableRect));
			for (int i = 0; i < count; i++) {
				new (&nitems[i]) RestorableRect(items[i]);
				items[i].~RestorableRect();
			}
			free(items);
			items = nitems;
			cap = ncap;
		}
		void destroy() {
			for (int i = 0; i < count; i++)
				items[i].~RestorableRect();
			free(items);
			items = NULL; count = 0; cap = 0;
		}
	};

	/* Edge-triggered debug keyboard hotkey. Formerly a class with an int
	 * constructor; now a plain struct driven by dbg_hotkey_init. query() does the
	 * rising-edge detection. TextureTracker embeds three of these by value and
	 * initialises them in its constructor. */
	struct DbgHotkey {
		retro_key key;
		bool was_key_down;

		bool query();
	};

	static void dbg_hotkey_init(DbgHotkey *h, retro_key key)
	{
		h->key = key;
		h->was_key_down = false;
	}

	struct CacheEntry {
		Rect rect;
		HdTextureHandle handle;
		CacheEntry() : rect(), handle(HdTextureHandle::make_none()) {}
	};

	/* Result of a handle-cache lookup (replaces std::pair<HdTextureHandle,bool>). */
	struct HandleCacheResult {
		HdTextureHandle handle;
		bool found;
		HandleCacheResult() : handle(HdTextureHandle::make_none()), found(false) {}
	};

	/* Small fixed-capacity move-to-front cache of recently-used HD texture
	 * handles. CacheEntry is POD, so the entries live in an inline array (capacity
	 * HANDLE_LRU_MAX) with a count - no std::vector, no heap. */
	enum { HANDLE_LRU_MAX = 4 };

	/* Small fixed-capacity move-to-front cache of recently-used HD texture
	 * handles. Formerly a class with a constructor; now a plain struct driven by
	 * handle_lru_cache_init. CacheEntry is POD (its defensive default-init is never
	 * read before being written - get only scans entries[0..count) and insert
	 * writes entries[0] within that range), so the entries live in an inline array
	 * (capacity HANDLE_LRU_MAX) with a count - no std::vector, no heap. */
	struct HandleLRUCache {
		int64_t dbg_hits;
		int64_t dbg_misses;
		int max_size;
		int count;
		CacheEntry entries[HANDLE_LRU_MAX];

		HandleCacheResult get(Rect rect, uint32_t palette_hash);
		void insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle);
		void clear()
		{
			count = 0;
		}
	};

	static void handle_lru_cache_init(HandleLRUCache *c, int max_size)
	{
		if (max_size > HANDLE_LRU_MAX)
			max_size = HANDLE_LRU_MAX;
		c->max_size = max_size;
		c->count = 0;
		/* The former constructor left these uninitialised (they were only ever
		 * zeroed inside a TT_LOG_VERBOSE block after first use); zero them here so
		 * the hit/miss counters start from a defined value. */
		c->dbg_hits = 0;
		c->dbg_misses = 0;
	}

	//========================================
	// Save State
	/* Owning hash -> TextureUpload map for the texture-tracker save state.
	 * Replaces std::map<uint32_t, TextureUpload>: each entry owns a heap
	 * TextureUpload (deep-copied in on insert, deleted on destroy). The values
	 * are non-trivially-copyable (owned image/dumped_modes/textures), so the
	 * backing array holds pointers - trivially relocatable - and ownership is
	 * explicit new/delete. The type is move-only: it is move-constructed up out
	 * of save_state() and move-assigned into the file-static save_state across
	 * renderer recreations, so move-assign must free any entries it already
	 * holds before adopting the source's. Copying is deleted to prevent any
	 * accidental double-ownership. */
	struct UploadOwningMap {
		struct Entry { uint32_t key; TextureUpload *val; };
		Entry *items;
		int count;
		int cap;

		UploadOwningMap() : items(NULL), count(0), cap(0) {}

		~UploadOwningMap() { destroy(); }

		UploadOwningMap(UploadOwningMap &&o) : items(o.items), count(o.count), cap(o.cap) {
			o.items = NULL; o.count = 0; o.cap = 0;
		}

		UploadOwningMap &operator=(UploadOwningMap &&o) {
			if (this != &o) {
				destroy();
				items = o.items; count = o.count; cap = o.cap;
				o.items = NULL; o.count = 0; o.cap = 0;
			}
			return *this;
		}

		/* No copies: the entries are owning pointers. */
		UploadOwningMap(const UploadOwningMap &) = delete;
		UploadOwningMap &operator=(const UploadOwningMap &) = delete;

		bool contains(uint32_t key) const {
			for (int i = 0; i < count; i++)
				if (items[i].key == key)
					return true;
			return false;
		}

		/* Takes ownership of an already-heap-allocated upload under key. Caller
		 * must have ensured the key is absent (mirrors the old map's insert-if-
		 * absent guard). */
		void insert(uint32_t key, TextureUpload *val) {
			if (count >= cap) {
				int ncap = cap ? cap * 2 : 8;
				Entry *nitems = (Entry *)realloc(items, (size_t)ncap * sizeof(Entry));
				if (!nitems)
					return;
				items = nitems;
				cap = ncap;
			}
			items[count].key = key;
			items[count].val = val;
			count++;
		}

	private:
		void destroy() {
			for (int i = 0; i < count; i++)
				texture_upload_destroy(items[i].val);
			free(items);
			items = NULL; count = 0; cap = 0;
		}
	};

	struct TextureRectSaveState {
		uint32_t upload_hash;
		int offset_x;
		int offset_y;
		SRect vram_rect;
	};

	/* Owning array of (POD) TextureRectSaveState. Replaces
	 * std::vector<TextureRectSaveState>. Elements are trivially relocatable, so
	 * grow is a plain realloc; the type owns its buffer (free on destroy) and is
	 * move-only so it can live inside the move-assigned save-state structs. */
	struct TextureRectSaveStateVec {
		TextureRectSaveState *items;
		int count;
		int cap;
	};
	static inline void TextureRectSaveStateVec_init(struct TextureRectSaveStateVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
	static inline void TextureRectSaveStateVec_free_storage(struct TextureRectSaveStateVec *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; }
	static inline int  TextureRectSaveStateVec_size(const struct TextureRectSaveStateVec *v) { return v->count; }
	static inline TextureRectSaveState *TextureRectSaveStateVec_at(struct TextureRectSaveStateVec *v, int i) { return &v->items[i]; }
	static inline void TextureRectSaveStateVec_push(struct TextureRectSaveStateVec *v, const TextureRectSaveState *valp) {
		if (v->count >= v->cap) {
			int ncap = v->cap ? v->cap * 2 : 8;
			TextureRectSaveState *nitems = (TextureRectSaveState *)realloc(v->items, (size_t)ncap * sizeof(TextureRectSaveState));
			if (!nitems)
				return;
			v->items = nitems;
			v->cap = ncap;
		}
		v->items[v->count++] = *valp;
	}
	/* Move src into dst (steal buffer), leaving src empty - replaces the implicit
	 * move of the former move-only container. */
	static inline void TextureRectSaveStateVec_move(struct TextureRectSaveStateVec *dst, struct TextureRectSaveStateVec *src) {
		dst->items = src->items; dst->count = src->count; dst->cap = src->cap;
		src->items = NULL; src->count = 0; src->cap = 0;
	}

	struct RestorableRectSaveState {
		Rect rect;
		uint32_t hash;
		TextureRectSaveStateVec to_restore;
	};
	static inline void rrss_init(struct RestorableRectSaveState *r) {
		r->rect.x = 0; r->rect.y = 0; r->rect.width = 0; r->rect.height = 0;
		r->hash = 0;
		TextureRectSaveStateVec_init(&r->to_restore);
	}
	static inline void rrss_destroy(struct RestorableRectSaveState *r) {
		TextureRectSaveStateVec_free_storage(&r->to_restore);
	}
	/* Move src into dst (steal to_restore), leaving src empty. */
	static inline void rrss_move(struct RestorableRectSaveState *dst, struct RestorableRectSaveState *src) {
		dst->rect = src->rect;
		dst->hash = src->hash;
		TextureRectSaveStateVec_move(&dst->to_restore, &src->to_restore);
	}

	/* Owning array of (move-only) RestorableRectSaveState. Replaces
	 * std::vector<RestorableRectSaveState>. The element owns a heap array
	 * (to_restore) and is not trivially relocatable, so growth move-constructs
	 * each element into the new storage via placement new and destroys the old
	 * one, rather than memcpy. Move-only so it composes inside the move-assigned
	 * save-state structs. */
	struct RestorableRectSaveStateVec {
		RestorableRectSaveState *items;
		int count;
		int cap;
	};
	static inline void RestorableRectSaveStateVec_init(struct RestorableRectSaveStateVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
	static inline int  RestorableRectSaveStateVec_size(const struct RestorableRectSaveStateVec *v) { return v->count; }
	static inline RestorableRectSaveState *RestorableRectSaveStateVec_at(struct RestorableRectSaveStateVec *v, int i) { return &v->items[i]; }
	static inline void RestorableRectSaveStateVec_grow(struct RestorableRectSaveStateVec *v, int ncap) {
		RestorableRectSaveState *nitems =
			(RestorableRectSaveState *)malloc((size_t)ncap * sizeof(RestorableRectSaveState));
		int i;
		for (i = 0; i < v->count; i++) {
			rrss_move(&nitems[i], &v->items[i]);
			rrss_destroy(&v->items[i]);
		}
		free(v->items);
		v->items = nitems;
		v->cap = ncap;
	}
	/* Takes ownership of *v's contents by move (src left empty). */
	static inline void RestorableRectSaveStateVec_push_move(struct RestorableRectSaveStateVec *v, struct RestorableRectSaveState *src) {
		if (v->count >= v->cap)
			RestorableRectSaveStateVec_grow(v, v->cap ? v->cap * 2 : 8);
		rrss_move(&v->items[v->count], src);
		v->count++;
	}
	static inline void RestorableRectSaveStateVec_free_storage(struct RestorableRectSaveStateVec *v) {
		int i;
		for (i = 0; i < v->count; i++)
			rrss_destroy(&v->items[i]);
		free(v->items);
		v->items = NULL; v->count = 0; v->cap = 0;
	}

	struct TextureTrackerSaveState {
		TextureRectSaveStateVec rects;
		RestorableRectSaveStateVec restorable;
		UploadOwningMap uploads;

		TextureTrackerSaveState() {
			TextureRectSaveStateVec_init(&rects);
			RestorableRectSaveStateVec_init(&restorable);
		}
		~TextureTrackerSaveState() {
			TextureRectSaveStateVec_free_storage(&rects);
			RestorableRectSaveStateVec_free_storage(&restorable);
		}
		TextureTrackerSaveState(TextureTrackerSaveState &&o) noexcept
			: uploads(static_cast<UploadOwningMap &&>(o.uploads)) {
			TextureRectSaveStateVec_move(&rects, &o.rects);
			restorable = o.restorable;
			RestorableRectSaveStateVec_init(&o.restorable);
		}
		TextureTrackerSaveState &operator=(TextureTrackerSaveState &&o) noexcept {
			if (this != &o) {
				TextureRectSaveStateVec_free_storage(&rects);
				RestorableRectSaveStateVec_free_storage(&restorable);
				TextureRectSaveStateVec_move(&rects, &o.rects);
				restorable = o.restorable;
				RestorableRectSaveStateVec_init(&o.restorable);
				uploads = static_cast<UploadOwningMap &&>(o.uploads);
			}
			return *this;
		}
		TextureTrackerSaveState(const TextureTrackerSaveState &) = delete;
		TextureTrackerSaveState &operator=(const TextureTrackerSaveState &) = delete;
	};
	// End of Save State
	//========================================

	/* Tunable HD-texture cache budgets. hd_cache = system RAM (decoded CPU levels);
	 * hd_gpu_cache = VRAM (uploaded Vulkan images). The VRAM budget targets 8 GB
	 * cards - lower it if you see VRAM-pressure "QueuePresent failed" / swapchain
	 * churn, raise it on larger cards. */
	static const size_t HD_CACHE_RAM_BUDGET  = (size_t)2 * 1024 * 1024 * 1024; /* 2 GB */
	static const size_t HD_CACHE_VRAM_BUDGET = (size_t)3 * 1024 * 1024 * 1024; /* 3 GB */

	/* (hash, palette_hash) packed into one 64-bit key. The caches below are keyed
	 * by this single integer instead of HdTextureId, so lookups compare one int
	 * with no struct operator< / tree descent. */
	static uint64_t hd_pack_key(HdTextureId id)
	{
		return ((uint64_t)id.hash << 32) | (uint64_t)id.palette_hash;
	}

	/* ------------------------------------------------------------------------- *
	 * Byte-budgeted LRU cache, MSVC C89.
	 *
	 * Replaces the original std::list<Entry> + std::map<id, list::iterator>
	 * design - two heap node allocations per insert and an O(log n) red-black
	 * descent per lookup - with a contiguous index-LRU + open-addressed
	 * (linear-probe) flat hash table, all held in malloc'd arrays. No templates,
	 * no classes, no virtual, no STL. The two cache variants (RAM levels, VRAM
	 * images) are produced by macro instantiation: HD_LRU_DECLARE emits the struct
	 * and prototypes, HD_LRU_DEFINE emits the bodies, with a DISPOSE callback that
	 * frees payload-owned resources when a live entry leaves the cache.
	 * ------------------------------------------------------------------------- */

	/* fmix64 - cheap, strong avalanche for integer keys. The multipliers are built
	 * from 32-bit halves so no C99 long-long (ULL) literals appear. */
	static uint64_t hd_lru_mix(uint64_t x)
	{
		uint64_t k1 = ((uint64_t)0xff51afd7u << 32) | (uint64_t)0xed558ccdu;
		uint64_t k2 = ((uint64_t)0xc4ceb9feu << 32) | (uint64_t)0x1a85ec53u;
		x ^= x >> 33;
		x *= k1;
		x ^= x >> 33;
		x *= k2;
		x ^= x >> 33;
		return x;
	}

#define HD_LRU_DECLARE(NAME, PAYLOAD_T)                                        \
	typedef struct NAME##_slot {                                                   \
		uint64_t  key;                                                             \
		int       prev;   /* LRU links by arena index (-1 = none) */               \
		int       next;                                                            \
		size_t    bytes;  /* cached payload footprint */                           \
		PAYLOAD_T payload;                                                         \
	} NAME##_slot;                                                                 \
	\
	typedef struct NAME {                                                          \
		NAME##_slot *slots;                                                        \
		int          slots_len;                                                    \
		int          slots_cap;                                                    \
		int         *table;        /* open-addressed slot indices; -1 = empty */   \
		size_t       table_len;    /* power of two, 0 if unallocated */            \
		size_t       mask;         /* table_len - 1 */                             \
		int          head, tail;   /* MRU / LRU ends */                            \
		int          free_head;    /* recycled-slot free list head */              \
		int          live;                                                         \
		size_t       total_bytes;                                                  \
		size_t       budget_bytes;                                                 \
	} NAME;                                                                        \
	\
	static void   NAME##_init(NAME *c, size_t budget_bytes);                       \
	static int    NAME##_contains(const NAME *c, uint64_t key);                    \
	static PAYLOAD_T *NAME##_get(NAME *c, uint64_t key);                           \
	static PAYLOAD_T *NAME##_put_slot(NAME *c, uint64_t key, int *created);        \
	static void   NAME##_account(NAME *c, size_t old_bytes, size_t new_bytes);     \
	static void   NAME##_erase(NAME *c, uint64_t key);                             \
	static void   NAME##_clear(NAME *c);                                           \
	static void   NAME##_set_entry_bytes(NAME *c, uint64_t key, size_t bytes);     \
	static void   NAME##_set_budget(NAME *c, size_t bytes);                        \
	static size_t NAME##_size_bytes(const NAME *c);                                \
	static size_t NAME##_count(const NAME *c);                                     \
	static size_t NAME##_budget(const NAME *c)

#define HD_LRU_DEFINE(NAME, PAYLOAD_T, DISPOSE)                                \
	static int NAME##_find_slot(const NAME *c, uint64_t key)                       \
	{                                                                              \
		size_t i;                                                                  \
		if (c->table_len == 0)                                                     \
		return -1;                                                             \
		i = (size_t)hd_lru_mix(key) & c->mask;                                     \
		for (;;) {                                                                 \
			int s = c->table[i];                                                   \
			if (s < 0)                                                             \
			return -1;                                                         \
			if (c->slots[s].key == key)                                            \
			return s;                                                          \
			i = (i + 1) & c->mask;                                                 \
		}                                                                          \
	}                                                                              \
	static void NAME##_raw_insert(NAME *c, uint64_t key, int s)                    \
	{                                                                              \
		size_t i = (size_t)hd_lru_mix(key) & c->mask;                             \
		while (c->table[i] >= 0)                                                   \
		i = (i + 1) & c->mask;                                                 \
		c->table[i] = s;                                                           \
	}                                                                              \
	static void NAME##_ensure_table(NAME *c, size_t want)                          \
	{                                                                              \
		size_t need = 8;                                                           \
		size_t i;                                                                  \
		int s;                                                                     \
		while (need < want * 2)                                                    \
		need <<= 1;                                                            \
		if (c->table_len != 0 && need <= c->table_len)                             \
		return;                                                                \
		{                                                                          \
			int *nt = (int *)realloc(c->table, need * sizeof(int));                \
			if (!nt)                                                               \
			return;                                                            \
			c->table = nt;                                                         \
		}                                                                          \
		c->table_len = need;                                                       \
		c->mask = need - 1;                                                        \
		for (i = 0; i < need; i++)                                                 \
		c->table[i] = -1;                                                      \
		for (s = c->head; s >= 0; s = c->slots[s].next)                           \
		NAME##_raw_insert(c, c->slots[s].key, s);                             \
	}                                                                              \
	static void NAME##_hash_remove(NAME *c, uint64_t key)                          \
	{                                                                              \
		size_t i = (size_t)hd_lru_mix(key) & c->mask;                             \
		size_t j;                                                                  \
		for (;;) {                                                                 \
			int s = c->table[i];                                                   \
			if (s >= 0 && c->slots[s].key == key)                                  \
			break;                                                             \
			i = (i + 1) & c->mask;                                                 \
		}                                                                          \
		j = i;                                                                     \
		c->table[i] = -1;                                                          \
		for (;;) {                                                                 \
			int sj;                                                                \
			size_t home;                                                          \
			int can_move;                                                          \
			j = (j + 1) & c->mask;                                                 \
			sj = c->table[j];                                                      \
			if (sj < 0)                                                            \
			break;                                                             \
			home = (size_t)hd_lru_mix(c->slots[sj].key) & c->mask;               \
			if (i <= j)                                                            \
			can_move = !(home > i && home <= j);                               \
			else                                                                   \
			can_move = !(home > i || home <= j);                               \
			if (can_move) {                                                        \
				c->table[i] = sj;                                                  \
				c->table[j] = -1;                                                  \
				i = j;                                                             \
			}                                                                      \
		}                                                                          \
	}                                                                              \
	static int NAME##_alloc_slot(NAME *c)                                          \
	{                                                                              \
		int s;                                                                     \
		if (c->free_head >= 0) {                                                   \
			s = c->free_head;                                                      \
			c->free_head = c->slots[s].next;                                       \
			return s;                                                              \
		}                                                                          \
		if (c->slots_len == c->slots_cap) {                                        \
			int ncap = c->slots_cap ? c->slots_cap * 2 : 16;                       \
			c->slots = (NAME##_slot *)realloc(c->slots,                            \
					(size_t)ncap * sizeof(NAME##_slot));                    \
			c->slots_cap = ncap;                                                   \
		}                                                                          \
		s = c->slots_len++;                                                        \
		c->slots[s].key = 0;                                                       \
		c->slots[s].prev = -1;                                                     \
		c->slots[s].next = -1;                                                     \
		c->slots[s].bytes = 0;                                                     \
		return s;                                                                  \
	}                                                                              \
	static void NAME##_free_slot(NAME *c, int s)                                   \
	{                                                                              \
		c->slots[s].next = c->free_head;                                          \
		c->free_head = s;                                                          \
	}                                                                              \
	static void NAME##_link_front(NAME *c, int s)                                  \
	{                                                                              \
		c->slots[s].prev = -1;                                                     \
		c->slots[s].next = c->head;                                                \
		if (c->head >= 0)                                                          \
		c->slots[c->head].prev = s;                                            \
		c->head = s;                                                               \
		if (c->tail < 0)                                                           \
		c->tail = s;                                                           \
	}                                                                              \
	static void NAME##_unlink(NAME *c, int s)                                      \
	{                                                                              \
		int p = c->slots[s].prev;                                                  \
		int n = c->slots[s].next;                                                  \
		if (p >= 0) c->slots[p].next = n; else c->head = n;                        \
		if (n >= 0) c->slots[n].prev = p; else c->tail = p;                        \
	}                                                                              \
	static void NAME##_touch(NAME *c, int s)                                       \
	{                                                                              \
		if (c->head == s)                                                          \
		return;                                                                \
		NAME##_unlink(c, s);                                                       \
		NAME##_link_front(c, s);                                                   \
	}                                                                              \
	static void NAME##_evict(NAME *c)                                              \
	{                                                                              \
		while (c->total_bytes > c->budget_bytes && c->tail >= 0) {                 \
			int s = c->tail;                                                       \
			uint64_t key = c->slots[s].key;                                        \
			c->total_bytes -= c->slots[s].bytes;                                   \
			DISPOSE(&c->slots[s].payload);                                         \
			NAME##_unlink(c, s);                                                   \
			NAME##_hash_remove(c, key);                                            \
			NAME##_free_slot(c, s);                                                \
			c->live--;                                                             \
		}                                                                          \
	}                                                                              \
	static void NAME##_init(NAME *c, size_t budget_bytes)                          \
	{                                                                              \
		c->slots = NULL; c->slots_len = 0; c->slots_cap = 0;                       \
		c->table = NULL; c->table_len = 0; c->mask = 0;                            \
		c->head = c->tail = c->free_head = -1;                                     \
		c->live = 0; c->total_bytes = 0; c->budget_bytes = budget_bytes;           \
	}                                                                              \
	static int NAME##_contains(const NAME *c, uint64_t key)                        \
	{                                                                              \
		return NAME##_find_slot(c, key) >= 0;                                      \
	}                                                                              \
	static PAYLOAD_T *NAME##_get(NAME *c, uint64_t key)                            \
	{                                                                              \
		int s = NAME##_find_slot(c, key);                                          \
		if (s < 0)                                                                 \
		return NULL;                                                           \
		NAME##_touch(c, s);                                                        \
		return &c->slots[s].payload;                                               \
	}                                                                              \
	static PAYLOAD_T *NAME##_put_slot(NAME *c, uint64_t key, int *created)         \
	{                                                                              \
		int s = NAME##_find_slot(c, key);                                          \
		if (s >= 0) {                                                              \
			*created = 0;                                                          \
			NAME##_touch(c, s);                                                    \
			return &c->slots[s].payload;                                           \
		}                                                                          \
		*created = 1;                                                              \
		NAME##_ensure_table(c, (size_t)c->live + 1);                               \
		s = NAME##_alloc_slot(c);                                                  \
		c->slots[s].key = key;                                                     \
		NAME##_link_front(c, s);                                                   \
		NAME##_raw_insert(c, key, s);                                              \
		c->live++;                                                                 \
		return &c->slots[s].payload;                                               \
	}                                                                              \
	static void NAME##_account(NAME *c, size_t old_bytes, size_t new_bytes)        \
	{                                                                              \
		c->total_bytes = c->total_bytes - old_bytes + new_bytes;                   \
		NAME##_evict(c);                                                           \
	}                                                                              \
	static void NAME##_erase(NAME *c, uint64_t key)                                \
	{                                                                              \
		int s = NAME##_find_slot(c, key);                                          \
		if (s < 0)                                                                 \
		return;                                                                \
		c->total_bytes -= c->slots[s].bytes;                                       \
		DISPOSE(&c->slots[s].payload);                                             \
		NAME##_unlink(c, s);                                                       \
		NAME##_hash_remove(c, key);                                                \
		NAME##_free_slot(c, s);                                                    \
		c->live--;                                                                 \
	}                                                                              \
	static void NAME##_clear(NAME *c)                                              \
	{                                                                              \
		int s;                                                                     \
		for (s = c->head; s >= 0; s = c->slots[s].next)                           \
		DISPOSE(&c->slots[s].payload);                                         \
		free(c->slots); free(c->table);                                            \
		c->slots = NULL; c->slots_len = 0; c->slots_cap = 0;                       \
		c->table = NULL; c->table_len = 0; c->mask = 0;                            \
		c->head = c->tail = c->free_head = -1;                                     \
		c->live = 0; c->total_bytes = 0;                                           \
	}                                                                              \
	static void NAME##_set_entry_bytes(NAME *c, uint64_t key, size_t bytes)        \
	{                                                                              \
		int s = NAME##_find_slot(c, key);                                          \
		if (s >= 0)                                                                \
		c->slots[s].bytes = bytes;                                             \
	}                                                                              \
	static void NAME##_set_budget(NAME *c, size_t bytes)                           \
	{                                                                              \
		c->budget_bytes = bytes;                                                   \
		NAME##_evict(c);                                                           \
	}                                                                              \
	static size_t NAME##_size_bytes(const NAME *c) { return c->total_bytes; }      \
	static size_t NAME##_count(const NAME *c)      { return (size_t)c->live; }     \
	static size_t NAME##_budget(const NAME *c)     { return c->budget_bytes; }

	/* --- RAM cache: decoded CPU levels, keyed by (hash, palette) ------------- *
	 * Lives independent of TextureUpload lifetime, so images survive the rapid
	 * VRAM upload churn of animated sprites. Decode-once: a combo is read+decoded
	 * from disk at most once until evicted. CPU-side only - the GPU upload happens
	 * on attach via Renderer::upload_texture, so it survives device/swapchain
	 * resets. The disposer frees the decoded levels. */
	typedef struct CachedHdImage {
		LoadedLevels levels; /* decoded RGBA + mips (CPU side) */
		int    alpha_flags;
		size_t bytes;
	} CachedHdImage;

	static void cached_hd_image_dispose(CachedHdImage *p)
	{
		loaded_levels_reset(&p->levels);
	}

	HD_LRU_DECLARE(HdImageCache, CachedHdImage);
	HD_LRU_DEFINE(HdImageCache, CachedHdImage, cached_hd_image_dispose)

		/* Insert/replace a combo's decoded levels; takes ownership of *levels (left
		 * empty on return). */
		static void hd_image_cache_put(HdImageCache *c, HdTextureId id,
				LoadedLevels *levels, int alpha_flags)
		{
			uint64_t key = hd_pack_key(id);
			int created = 0;
			CachedHdImage *e = HdImageCache_put_slot(c, key, &created);
			size_t old_bytes = created ? 0 : e->bytes;
			if (created)
				loaded_levels_init(&e->levels); /* fresh slot: payload is indeterminate */
			loaded_levels_move(&e->levels, levels);
			e->alpha_flags = alpha_flags;
			e->bytes = loaded_levels_byte_size(&e->levels);
			HdImageCache_set_entry_bytes(c, key, e->bytes);
			HdImageCache_account(c, old_bytes, e->bytes);
		}

	/* --- VRAM cache: uploaded Vulkan images, keyed by (hash, palette) -------- *
	 * Sits above the RAM cache. Re-attaching a combo to a recreated TextureUpload
	 * becomes a ref-counted handle copy instead of a fresh upload_texture. The
	 * image is held as a RAW Image* (trivially relocatable, so the malloc
	 * arena can realloc it) with the refcount managed by hand: a reference is
	 * taken on insert and released by the disposer on eviction/clear, freeing VRAM
	 * once no live draw still holds the image. */
	typedef struct CachedGpuImage {
		Image *image; /* owns one reference while resident (NULL = empty) */
		int    alpha_flags;
		size_t bytes;         /* approximate VRAM footprint (== decoded levels size) */
	} CachedGpuImage;

	static void cached_gpu_image_dispose(CachedGpuImage *p)
	{
		if (p->image) {
			image_release_reference(p->image);
			p->image = NULL;
		}
	}

	HD_LRU_DECLARE(HdGpuCache, CachedGpuImage);
	HD_LRU_DEFINE(HdGpuCache, CachedGpuImage, cached_gpu_image_dispose)

		/* Take a counted reference to a cached image and return it as an ImageHandle
		 * the caller can store/copy/destroy normally. IntrusivePtr(T*) adopts without
		 * bumping, so add the reference explicitly first. */
		static ImageHandle hd_gpu_image_handle(CachedGpuImage *g)
		{
			image_add_reference(g->image);
			return ih_make(g->image);
		}

	/* Insert/replace a combo's GPU image. Adds a reference to `handle`'s image
	 * (held until eviction); a prior image at this key is released first. */
	static void hd_gpu_cache_put(HdGpuCache *c, HdTextureId id,
			ImageHandle handle, int alpha_flags, size_t bytes)
	{
		uint64_t key = hd_pack_key(id);
		int created = 0;
		CachedGpuImage *e = HdGpuCache_put_slot(c, key, &created);
		size_t old_bytes = created ? 0 : e->bytes;
		if (!created && e->image)
			image_release_reference(e->image); /* drop the image we're replacing */
		e->image = ih_get(&handle);
		if (e->image)
			image_add_reference(e->image);      /* cache holds its own reference */
		e->alpha_flags = alpha_flags;
		e->bytes = bytes;
		HdGpuCache_set_entry_bytes(c, key, bytes);
		HdGpuCache_account(c, old_bytes, bytes);
	}

	/* ------------------------------------------------------------------------- *
	 * HdKeySet - an ordered set of packed (hash, palette) uint64_t keys, MSVC C89.
	 *
	 * Replaces std::set<HdTextureId>. Backed by a single sorted, malloc'd
	 * uint64_t array: membership is O(log n) binary search, insert/erase keep it
	 * sorted (O(n) shift, fine for these small sets). Because the key packs
	 * (hash << 32) | palette, the array is ordered by hash then palette, so all
	 * combos of one hash form a contiguous run - found with lower_bound over the
	 * half-open key range [hash<<32, (hash+1)<<32), which is exactly what the old
	 * std::set lower_bound/upper_bound({hash,0})/({hash,0xFFFFFFFF}) gave.
	 * ------------------------------------------------------------------------- */
	typedef struct HdKeySet {
		uint64_t *keys;
		int       count;
		int       cap;
	} HdKeySet;

	static void hd_key_set_init(HdKeySet *s)
	{
		s->keys = NULL;
		s->count = 0;
		s->cap = 0;
	}
	static void hd_key_set_free(HdKeySet *s)
	{
		free(s->keys);
		s->keys = NULL;
		s->count = 0;
		s->cap = 0;
	}
	static void hd_key_set_clear(HdKeySet *s)
	{
		s->count = 0; /* keep the allocation for reuse */
	}
	/* First index with keys[i] >= key (i.e. std::lower_bound). */
	static int hd_key_set_lower_bound(const HdKeySet *s, uint64_t key)
	{
		int lo = 0, hi = s->count;
		while (lo < hi) {
			int mid = lo + ((hi - lo) >> 1);
			if (s->keys[mid] < key)
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	}
	static int hd_key_set_contains(const HdKeySet *s, uint64_t key)
	{
		int i = hd_key_set_lower_bound(s, key);
		return i < s->count && s->keys[i] == key;
	}
	/* Insert key; returns 1 if newly added, 0 if already present. */
	static int hd_key_set_insert(HdKeySet *s, uint64_t key)
	{
		int i = hd_key_set_lower_bound(s, key);
		if (i < s->count && s->keys[i] == key)
			return 0;
		if (s->count == s->cap) {
			int ncap = s->cap ? s->cap * 2 : 16;
			uint64_t *nk = (uint64_t *)realloc(s->keys, (size_t)ncap * sizeof(uint64_t));
			if (!nk)
				return 0;
			s->keys = nk;
			s->cap = ncap;
		}
		memmove(&s->keys[i + 1], &s->keys[i], (size_t)(s->count - i) * sizeof(uint64_t));
		s->keys[i] = key;
		s->count++;
		return 1;
	}
	static void hd_key_set_erase(HdKeySet *s, uint64_t key)
	{
		int i = hd_key_set_lower_bound(s, key);
		if (i >= s->count || s->keys[i] != key)
			return;
		memmove(&s->keys[i], &s->keys[i + 1], (size_t)(s->count - i - 1) * sizeof(uint64_t));
		s->count--;
	}

	/* TextureTracker: the HD-texture replacement engine -- tracks VRAM rects,
	 * resolves (hash,palette) combos to HD textures, and drives the disk/CPU/GPU
	 * cache tiers and the IO worker thread. Converted from a C++ class to a plain C
	 * struct + texture_tracker_* free functions. The ctor/dtor become
	 * texture_tracker_init / texture_tracker_fini; the embedded RectTracker /
	 * FusedPages are init'd/deinit'd there (already converted). Default member
	 * initializers move into texture_tracker_init. */
	struct TextureTracker {
		bool dump_enabled;
		bool hd_textures_enabled;
		bool eager_textures; // true = prefetch all palettes of a hash on upload (master-consistent); false = lazy per-draw

		IOThread iothread;
		Renderer *uploader;

		ImageHandle default_hd_texture;

		RectMatch dump_ignore[DUMP_IGNORE_MAX];
		int       dump_ignore_count;

		HdKeySet known_files;
		// Palette-hash cache: a plain growable array (append-only until cleared,
		// no eviction/order), replacing std::vector<CachedPaletteHash>.
		CachedPaletteHash *cached_palette_hashes;
		int cached_palette_hashes_count;
		int cached_palette_hashes_cap;
		RestorableRectVec restorable_rects;
		FusedPages fused_pages;
		uint64_t frame;

		RectTracker tracker;
		HandleLRUCache handle_cache;

		// HD image caches, independent of upload lifetime. Tier 1 = GPU (VRAM,
		// ready-to-bind Vulkan images); tier 2 = CPU (RAM, decoded levels); tier 3
		// = disk. Initialised via HdGpuCache_init / HdImageCache_init.
		HdGpuCache hd_gpu_cache;
		HdImageCache hd_cache;
		HdKeySet requested;        // disk load in flight, or known to have no file (negative cache)
		HdKeySet pending_attach;   // cached combos drawn/decoded this frame, awaiting GPU attach at on_queues_reset

		// Diagnostics (logged every 300 frames by endFrame).
		uint64_t dbg_responses_received;
		uint64_t dbg_responses_received_last;
		uint64_t dbg_gpu_uploads;
		uint64_t dbg_gpu_uploads_last;
		uint64_t dbg_attaches;
		uint64_t dbg_attaches_last;

		DbgHotkey frame_dump_key;
		RFILE *frame_dump;
		bool frame_dump_need_comma;

		DbgHotkey hd_toggle_key;
		DbgHotkey reload_key;
		DbgHotkey fastpath_key;
		bool fastpath_enabled;
	};

	void texture_tracker_init(struct TextureTracker *self);
	void texture_tracker_fini(struct TextureTracker *self);

	TextureTrackerSaveState texture_tracker_save_state(struct TextureTracker *self);
	void texture_tracker_load_state(struct TextureTracker *self, const TextureTrackerSaveState &state);

	void texture_tracker_upload(struct TextureTracker *self, Rect rect, uint16_t *vram);
	void texture_tracker_blit(struct TextureTracker *self, Rect dst, Rect src);
	void texture_tracker_clearRegion(struct TextureTracker *self, Rect rect);
	void texture_tracker_notifyReadback(struct TextureTracker *self, Rect rect, uint16_t *vram);
	uint32_t texture_tracker_dbgHashVram(struct TextureTracker *self, Rect rect, uint16_t *vram);

	HdTextureHandle texture_tracker_get_hd_texture_index(struct TextureTracker *self, Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable, bool &cache_hit);
	HdTexture texture_tracker_get_hd_texture(struct TextureTracker *self, HdTextureHandle index);
	void texture_tracker_endFrame(struct TextureTracker *self);
	void texture_tracker_on_queues_reset(struct TextureTracker *self);

	void texture_tracker_set_texture_uploader(struct TextureTracker *self, Renderer *t);

	static inline void texture_tracker_set_cache_budgets(struct TextureTracker *self, size_t ram_bytes, size_t vram_bytes)
	{
		HdImageCache_set_budget(&self->hd_cache, ram_bytes);
		HdGpuCache_set_budget(&self->hd_gpu_cache, vram_bytes);
	}

	static Palette texture_tracker_get_palette(struct TextureTracker *self, Rect palette_rect);
	static uint32_t texture_tracker_get_palette_hash(struct TextureTracker *self, Rect palette_rect);
	static void texture_tracker_want_combo(struct TextureTracker *self, HdTextureId id);
	static void texture_tracker_dump_texture(struct TextureTracker *self, TextureUpload *upload, UsedMode &mode, DumpedMode dump_mode);
	static void texture_tracker_load_hd_texture(struct TextureTracker *self, uint32_t hash);
	static void texture_tracker_request_hd_texture(struct TextureTracker *self, TextureUpload *upload, uint32_t palette_hash);
	static void texture_tracker_reload_textures_from_disk(struct TextureTracker *self);
	static void texture_tracker_dump_image(struct TextureTracker *self, TextureUpload &upload, UsedMode &mode);
	static inline void texture_tracker_clear_palette_cache(struct TextureTracker *self, Rect rect)
	{
		self->cached_palette_hashes_count = 0; /* keep allocation for reuse */
	}
	static TextureUpload *texture_tracker_find_upload(struct TextureTracker *self, uint32_t hash);

	struct Vertex
	{
		float x, y, w;
		uint32_t color;
		uint16_t u, v;
	};

	struct TextureWindow
	{
		uint8_t mask_x, mask_y, or_x, or_y;
	};

	struct UVRect
	{
		uint16_t min_u, min_v, max_u, max_v;
	};

	enum SemiTransparentMode {
		SemiTransparentMode_None,
		SemiTransparentMode_Average,
		SemiTransparentMode_Add,
		SemiTransparentMode_Sub,
		SemiTransparentMode_AddQuarter
	};

	enum PrimitiveType {
		PrimitiveType_Sprite,
		PrimitiveType_Polygon,
		PrimitiveType_May_Be_2D_Polygon
	};

	/* Display rectangle (signed origin, unlike Rect). Hoisted out of Renderer to
	 * file scope and de-C++'d: the 4-arg constructor becomes display_rect_make and
	 * the default-member-initialisers are dropped (every constructed DisplayRect is
	 * fully assigned). */
	struct DisplayRect
	{
		// Unlike Rect, the x-y coordinates for a DisplayRect can be negative
		int x;
		int y;
		unsigned width;
		unsigned height;
	};

	static inline struct DisplayRect display_rect_make(int x, int y, unsigned width, unsigned height)
	{
		struct DisplayRect r;
		r.x = x;
		r.y = y;
		r.width = width;
		r.height = height;
		return r;
	}

	class Renderer
	{
		public:
			enum ScanoutMode {
				// Use extra precision bits.
				ScanoutMode_ABGR1555_555,
				// Use extra precision bits to dither down to a native ABGR1555 image.
				// The dither happens in the wrong place, but should be "good" enough to feel authentic.
				ScanoutMode_ABGR1555_Dither,
				// MDEC
				ScanoutMode_BGR24
			};

			enum ScanoutFilter {
				ScanoutFilter_None,
				ScanoutFilter_SSAA,
				ScanoutFilter_MDEC_YUV
			};

			enum WidthMode {
				WidthMode_WIDTH_MODE_256 = 0,
				WidthMode_WIDTH_MODE_320 = 1,
				WidthMode_WIDTH_MODE_512 = 2,
				WidthMode_WIDTH_MODE_640 = 3,
				WidthMode_WIDTH_MODE_368 = 4
			};

			struct RenderState
			{
				//Rect display_mode;
				Rect display_fb_rect;
				TextureWindow texture_window;
				Rect cached_window_rect;
				Rect draw_rect;
				int draw_offset_x = 0;
				int draw_offset_y = 0;
				unsigned palette_offset_x = 0;
				unsigned palette_offset_y = 0;
				unsigned texture_offset_x = 0;
				unsigned texture_offset_y = 0;

				int vert_start = 0x10;
				int vert_end = 0x100;
				int horiz_start = 0x200;
				int horiz_end = 0xC00;

				bool is_pal = false;
				bool is_480i = false;
				WidthMode width_mode = WidthMode_WIDTH_MODE_320;
				int crop_overscan = 0;
				unsigned image_crop = 0;

				// Experimental horizontal offset feature
				int offset_cycles = 0;

				int slstart = 0;
				int slend = 239;

				int slstart_pal = 0;
				int slend_pal = 287;

				unsigned display_fb_xstart = 0;
				unsigned display_fb_ystart = 0;

				TextureMode texture_mode = TextureMode_None;
				SemiTransparentMode semi_transparent = SemiTransparentMode_None;
				PrimitiveType primitive_type = PrimitiveType_Polygon;
				ScanoutMode scanout_mode = ScanoutMode_ABGR1555_555;
				ScanoutFilter scanout_filter = ScanoutFilter_None;
				ScanoutFilter scanout_mdec_filter = ScanoutFilter_None;
				bool dither_native_resolution = false;
				bool force_mask_bit = false;
				bool texture_color_modulate = false;
				bool mask_test = false;
				bool display_on = false;
				bool adaptive_smoothing = true;

				UVRect UVLimits;
			};

			/* Owning uint32_t buffer for the saved VRAM image. Replaces
			 * std::vector<uint32_t>; like the other save-state members it lives
			 * in a file-static that is move-assigned across renderer
			 * recreations, so it is move-only with a free-existing move-assign
			 * and a destructor, and copying is deleted. */
			struct OwnedU32Buf
			{
				uint32_t *items;
				size_t    n;

				OwnedU32Buf() : items(NULL), n(0) {}
				~OwnedU32Buf() { free(items); }

				OwnedU32Buf(OwnedU32Buf &&o) : items(o.items), n(o.n) { o.items = NULL; o.n = 0; }
				OwnedU32Buf &operator=(OwnedU32Buf &&o)
				{
					if (this != &o)
					{
						free(items);
						items = o.items; n = o.n;
						o.items = NULL; o.n = 0;
					}
					return *this;
				}
				OwnedU32Buf(const OwnedU32Buf &) = delete;
				OwnedU32Buf &operator=(const OwnedU32Buf &) = delete;

				/* Deep-copy n elements from src into a fresh buffer. */
				void assign(const uint32_t *src, size_t count)
				{
					free(items);
					items = NULL; n = 0;
					if (count)
					{
						items = (uint32_t *)malloc(count * sizeof(uint32_t));
						memcpy(items, src, count * sizeof(uint32_t));
						n = count;
					}
				}

				const uint32_t *data() const { return items; }
				bool empty() const { return n == 0; }
			};

			struct SaveState
			{
				OwnedU32Buf vram;
				RenderState state;
				TextureTrackerSaveState tracker_state;
			};

			Renderer(Device &device, unsigned scaling, unsigned msaa, const SaveState *save_state);
			~Renderer();

			void set_track_textures(bool enable)
			{
				texture_tracking_enabled = enable;
			}
			void set_dump_textures(bool enable)
			{
				tracker.dump_enabled = enable;
			}
			void set_replace_textures(bool enable)
			{
				tracker.hd_textures_enabled = enable;
			}
			void set_hd_cache_budgets(size_t ram_bytes, size_t vram_bytes)
			{
				texture_tracker_set_cache_budgets(&tracker, ram_bytes, vram_bytes);
			}
			void set_eager_hd_textures(bool enable)
			{
				tracker.eager_textures = enable;
			}

			void set_adaptive_smoothing(bool enable)
			{
				render_state.adaptive_smoothing = enable;
			}

			void set_draw_rect(const Rect &rect);
			inline void set_draw_offset(int x, int y)
			{
				render_state.draw_offset_x = x;
				render_state.draw_offset_y = y;
			}

			inline void set_scissored_invariant(bool invariant)
			{
				queue.scissor_invariant = invariant;
			}

			void set_texture_window(const TextureWindow &window)
			{
				render_state.texture_window = window;
				render_state.cached_window_rect = compute_window_rect(window);
			}
			inline void set_texture_offset(unsigned x, unsigned y)
			{
				fbatlas_set_texture_offset(&atlas, x, y);
				render_state.texture_offset_x = x;
				render_state.texture_offset_y = y;
			}

			inline void set_palette_offset(unsigned x, unsigned y)
			{
				fbatlas_set_palette_offset(&atlas, x, y);
				render_state.palette_offset_x = x;
				render_state.palette_offset_y = y;
			}

			BufferHandle copy_cpu_to_vram(const Rect &rect);
			void copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram);
			uint16_t *begin_copy(BufferHandle handle)
			{
				return (uint16_t *)(device->map_host_buffer(*bh_get(&handle), MEMORY_ACCESS_WRITE_BIT));
			}
			void end_copy(BufferHandle handle)
			{
				device->unmap_host_buffer(*bh_get(&handle), MEMORY_ACCESS_WRITE_BIT);
			}

			void notify_texture_upload(Rect rect, uint16_t *vram)
			{
				if (texture_tracking_enabled)
					texture_tracker_upload(&tracker, rect, vram);
			}

			void blit_vram(const Rect &dst, const Rect &src);

			void set_vram_framebuffer_coords(unsigned xstart, unsigned ystart)
			{
				ih_reset(&last_scanout);

				render_state.display_fb_xstart = xstart;
				render_state.display_fb_ystart = ystart;
			}

			void set_horizontal_display_range(int x1, int x2)
			{
				render_state.horiz_start = x1;
				render_state.horiz_end = x2;
			}

			void set_vertical_display_range(int y1, int y2)
			{
				render_state.vert_start = y1;
				render_state.vert_end = y2;
			}

			void set_display_mode(ScanoutMode mode, bool is_pal, bool is_480i, WidthMode width_mode)
			{
				//if (rect != render_state.display_mode || render_state.scanout_mode != mode)
				//	ih_reset(&last_scanout);
				ih_reset(&last_scanout);

				//render_state.display_mode = rect;
				render_state.scanout_mode = mode;

				render_state.is_pal = is_pal;
				render_state.is_480i = is_480i;
				render_state.width_mode = width_mode;
			}

			void set_horizontal_overscan_cropping(int crop_overscan)
			{
				render_state.crop_overscan = crop_overscan;
			}

			void set_horizontal_offset_cycles(int offset_cycles)
			{
				render_state.offset_cycles = offset_cycles;
			}

			void set_horizontal_additional_cropping(unsigned image_crop)
			{
				render_state.image_crop = image_crop;
			}

			void set_visible_scanlines(int slstart, int slend, int slstart_pal, int slend_pal)
			{
				// May need bounds checking to reject bad inputs. Currently assume all inputs are valid.
				render_state.slstart = slstart;
				render_state.slend = slend;
				render_state.slstart_pal = slstart_pal;
				render_state.slend_pal = slend_pal;
			}

			void set_display_filter(ScanoutFilter filter)
			{
				render_state.scanout_filter = filter;
			}

			void set_mdec_filter(ScanoutFilter mdec_filter)
			{
				render_state.scanout_mdec_filter = mdec_filter;
			}

			void toggle_display(bool enable)
			{
				if (enable != render_state.display_on)
					ih_reset(&last_scanout);

				render_state.display_on = enable;
			}

			void set_dither_native_resolution(bool enable)
			{
				render_state.dither_native_resolution = enable;
			}

			ImageHandle scanout_vram_to_texture(bool scaled = true);
			ImageHandle scanout_to_texture();

			inline void set_texture_mode(TextureMode mode)
			{
				render_state.texture_mode = mode;
				fbatlas_set_texture_mode(&atlas, mode);
			}

			inline void set_semi_transparent(SemiTransparentMode state)
			{
				render_state.semi_transparent = state;
			}

			inline void set_primitive_type(PrimitiveType primitive_type)
			{
				render_state.primitive_type = primitive_type;
			}

			inline void set_force_mask_bit(bool enable)
			{
				render_state.force_mask_bit = enable;
			}

			inline void set_mask_test(bool enable)
			{
				render_state.mask_test = enable;
			}

			inline void set_texture_color_modulate(bool enable)
			{
				render_state.texture_color_modulate = enable;
			}

			inline void set_UV_limits(uint16_t min_u, uint16_t min_v, uint16_t max_u, uint16_t max_v)
			{
				render_state.UVLimits.min_u = min_u;
				render_state.UVLimits.min_v = min_v;
				render_state.UVLimits.max_u = max_u;
				render_state.UVLimits.max_v = max_v;
			}

			// Draw commands
			void clear_rect(const Rect &rect, uint32_t fb_color);
			void draw_line(const Vertex *vertices);
			void draw_triangle(const Vertex *vertices);
			void draw_quad(const Vertex *vertices);

			SaveState save_vram_state();

			void flush()
			{
				if (cbh_is_valid(&cmd))
					device->submit(cmd);
				cbh_reset(&cmd);
				device->flush_frame();
			}

			Fence flush_and_signal()
			{
				Fence fence;
				fence.data = NULL;
				if (cbh_is_valid(&cmd))
					device->submit(cmd, &fence);
				cbh_reset(&cmd);
				device->flush_frame();
				/* Return by value transfers ownership to the caller; the
				 * struct copy does not incref and this local must NOT reset
				 * (moved-from). */
				return fence;
			}

			enum
			{
				SpecConstIndex_TransMode = 0,
				SpecConstIndex_FilterMode = 1,
				SpecConstIndex_BlendMode = 2,
				SpecConstIndex_Scaling = 3,
				SpecConstIndex_Shift = 4,
				SpecConstIndex_OffsetUV = 5,
				SpecConstIndex_Samples = 0,
			};

			enum FilterExclude
			{
				FilterExcludeNone = 0,
				FilterExcludeOpaque = 1,
				FilterExcludeOpaqueAndSemiTrans = 2,
			};

			enum FilterMode {
			FilterMode_NearestNeighbor = 0,
			FilterMode_XBR = 1,
			FilterMode_SABR = 2,
			FilterMode_Bilinear = 3,
			FilterMode_Bilinear3Point = 4,
			FilterMode_JINC2 = 5
		};

			enum TransMode {
			TransMode_Opaque = 0,
			TransMode_SemiTrans = 1,
			TransMode_SemiTransOpaque = 2
		};

			enum BlendMode {
			BlendMode_BlendAdd = 0,
			BlendMode_BlendAvg = 1,
			BlendMode_BlendSub = 2,
			BlendMode_BlendAddQuarter = 3
		};

			void set_filter_mode(FilterMode mode)
			{
				primitive_filter_mode = mode;
			}
			ScanoutMode get_scanout_mode() const
			{
				return render_state.scanout_mode;
			}

			void set_sprite_filter_exclude(FilterExclude exclude)
			{
				sprite_filter_exclude = exclude;
			}

			void set_polygon_2d_filter_exclude(FilterExclude exclude)
			{
				polygon_2d_filter_exclude = exclude;
			}

			void set_scaled_uv_offset(bool offset)
			{
				scaled_uv_offset = offset;
			}

			/* True iff the constructor finished successfully. The Renderer
			 * constructor does not throw; on failure (e.g. RGBA8_UNORM not
			 * supported) it leaves the object in a destroyable but otherwise
			 * unusable state. Callers must check is_valid() before use. */
			bool is_valid() const { return valid; }

		private:
			Device *device;
			unsigned scaling;
			unsigned msaa;
			bool scaled_uv_offset = false;
			bool valid = false;
			FilterMode primitive_filter_mode = FilterMode_NearestNeighbor;
			FilterExclude sprite_filter_exclude = FilterExcludeNone;
			FilterExclude polygon_2d_filter_exclude = FilterExcludeNone;
			ImageHandle scaled_framebuffer;
			ImageHandle scaled_framebuffer_msaa;
			ImageHandle bias_framebuffer;
			ImageHandle framebuffer;
			ImageHandle framebuffer_ssaa;
			ImageViewHandleVec scaled_views;
			FBAtlas atlas;
			bool texture_tracking_enabled = false;
			TextureTracker tracker;

			CommandBufferHandle cmd;

		public:
			// Called by FBAtlas (formerly via HazardListener interface).
			void hazard(StatusFlags flags);
			void resolve(Domain target_domain, unsigned x, unsigned y);
			void flush_render_pass(const Rect &rect);
			void discard_render_pass()
			{
				reset_queue();
			}
			void clear_quad(const Rect &rect, uint32_t fb_color, bool candidate);

			// Called by TextureTracker (formerly via TextureUploader interface).
			ImageHandle upload_texture(LoadedLevels &image);
			ImageHandle create_texture(int width, int height, int levels);
			CommandBufferHandle &command_buffer_hack_fixme()
			{
				ensure_command_buffer();
				return cmd;
			}

		private:
			void hd_texture_uniforms(HdTextureHandle hd_texture_index);
			HdTextureHandle get_hd_texture_index(const Rect &uvlimits, bool &fastpath_capable_out, bool &cache_hit_out);

			struct
			{
				Program *copy_to_vram;
				Program *copy_to_vram_masked;
				Program *unscaled_quad_blitter;
				Program *scaled_quad_blitter;
				Program *unscaled_dither_quad_blitter;
				Program *scaled_dither_quad_blitter;
				Program *bpp24_quad_blitter;
				Program *bpp24_yuv_quad_blitter;
				Program *resolve_to_scaled;
				Program *resolve_to_unscaled;

				Program *blit_vram_scaled;
				Program *blit_vram_scaled_masked;

				Program *blit_vram_cached_scaled;
				Program *blit_vram_cached_scaled_masked;
				Program *blit_vram_msaa_cached_scaled;
				Program *blit_vram_msaa_cached_scaled_masked;

				Program *blit_vram_unscaled;
				Program *blit_vram_unscaled_masked;
				Program *blit_vram_cached_unscaled;
				Program *blit_vram_cached_unscaled_masked;

				Program *flat;
				Program *textured_scaled;
				Program *textured_unscaled;
				Program *flat_masked;
				Program *textured_masked_scaled;
				Program *textured_masked_unscaled;

				Program *mipmap_resolve;
				Program *mipmap_dither_resolve;
				Program *mipmap_energy_first;
				Program *mipmap_energy;
				Program *mipmap_energy_blur;
			} pipelines;

			ImageHandle dither_lut;

			void init_pipelines();
			void init_primitive_pipelines();
			void init_primitive_feedback_pipelines();
			void ensure_command_buffer();

			RenderState render_state;

			struct BufferVertex
			{
				float x, y, z, w;
				uint32_t color;
				TextureWindow window;
				int16_t pal_x, pal_y, params;
				int16_t u, v, base_uv_x, base_uv_y;
				uint16_t min_u, min_v, max_u, max_v;
			};

			struct BlitInfo
			{
				uint32_t src_offset[2];
				uint32_t dst_offset[2];
				uint32_t extent[2];
				uint32_t mask;
				uint32_t sample;
			};

			struct SemiTransparentState
			{
				int scissor_index;
				HdTextureHandle hd_texture_index;
				SemiTransparentMode semi_transparent;
				bool textured;
				bool masked;
				bool filtering;
				bool scaled_read;
				unsigned shift;
				bool offset_uv;

				bool operator==(const SemiTransparentState &other) const
				{
					return scissor_index == other.scissor_index && hd_texture_index == other.hd_texture_index &&
						semi_transparent == other.semi_transparent && textured == other.textured && masked == other.masked &&
						filtering == other.filtering && scaled_read == other.scaled_read && shift == other.shift &&
						offset_uv == other.offset_uv;
				}

				bool operator!=(const SemiTransparentState &other) const
				{
					return !(*this == other);
				}
			};

			struct ClearCandidate
			{
				Rect rect;
				uint32_t color; /* fb_color */
				float z;
			};

			struct PrimitiveInfo {
				unsigned triangle_index;
				int scissor_index;
				HdTextureHandle hd_texture_index;
				bool filtering;
				bool scaled_read;
				unsigned shift;
				bool offset_uv;

				// needed for emplace_back
				PrimitiveInfo(
						unsigned triangle_index,
						int scissor_index = -1,
						HdTextureHandle hd_texture_index = HdTextureHandle::make_none(),
						bool filtering = false,
						bool scaled_read = false,
						unsigned shift = 0,
						bool offset_uv = false
					     )
					: triangle_index(triangle_index), scissor_index(scissor_index), hd_texture_index(hd_texture_index),
					filtering(filtering), scaled_read(scaled_read), shift(shift), offset_uv(offset_uv)
				{}
			};

			POD_VEC_DECLARE(BufferVertexVec, BufferVertex);
			POD_VEC_DECLARE(PrimitiveInfoVec, PrimitiveInfo);
			POD_VEC_DECLARE(SemiTransparentStateVec, SemiTransparentState);
			POD_VEC_DECLARE(BlitInfoVec, BlitInfo);
			POD_VEC_DECLARE(ClearCandidateVec, ClearCandidate);
			POD_VEC_DECLARE(Rect2DVec, VkRect2D);

			struct OpaqueQueue
			{
				// Non-textured primitives.
				BufferVertexVec opaque{};
				PrimitiveInfoVec opaque_scissor{};

				// Textured primitives, no semi-transparency.
				BufferVertexVec opaque_textured{};
				PrimitiveInfoVec opaque_textured_scissor{};

				// Textured primitives, semi-transparency enabled.
				BufferVertexVec semi_transparent_opaque{};
				PrimitiveInfoVec semi_transparent_opaque_scissor{};

				BufferVertexVec semi_transparent{};
				SemiTransparentStateVec semi_transparent_state{};

				Rect2DVec scaled_resolves{};
				Rect2DVec unscaled_resolves{};
				BlitInfoVec scaled_blits{};
				BlitInfoVec scaled_masked_blits{};
				BlitInfoVec unscaled_blits{};
				BlitInfoVec unscaled_masked_blits{};

				Rect2DVec scissors{};
				ClearCandidateVec clear_candidates{};
				VkRect2D default_scissor;
				bool scissor_invariant = false;
			} queue;
			unsigned primitive_index = 0;
			bool render_pass_is_feedback = false;
			float last_uv_scale_x, last_uv_scale_y;

			void dispatch(const BufferVertexVec &vertices, PrimitiveInfoVec &scissors, bool textured = false);
			static bool primitive_info_sort_gt(const PrimitiveInfo &a, const PrimitiveInfo &b);
			static int primitive_info_qsort_cmp(const void *a, const void *b);
			void render_opaque_primitives();
			void render_opaque_texture_primitives();
			void render_semi_transparent_opaque_texture_primitives();
			void render_semi_transparent_primitives();
			void semi_transparent_set_state(const SemiTransparentState &state);
			void dispatch_set_scaled_read_texture(bool scaled_read, bool textured);
			void reset_queue();

			float allocate_depth(Domain domain, const Rect &rect);

			void build_attribs(BufferVertex *verts, const Vertex *vertices, unsigned count, HdTextureHandle &hd_texture_index,
					bool &filtering, bool &scaled_read, unsigned &shift, bool &offset_uv);
			void build_line_quad(Vertex *quad, const Vertex *line);
			BufferVertexVec *select_pipeline(unsigned prims, int scissor, HdTextureHandle hd_texture,
					bool filtering, bool scaled_read, unsigned shift, bool offset_uv);
			bool get_filer_exclude(FilterExclude exclude)
			{
				if (
						render_state.primitive_type == PrimitiveType_Sprite &&
						sprite_filter_exclude >= exclude
				   )
					return true;
				if (
						render_state.primitive_type == PrimitiveType_May_Be_2D_Polygon &&
						polygon_2d_filter_exclude >= exclude
				   )
					return true;
				return false;
			}

			void flush_resolves();
			void flush_blits();
			void flush_blit(const BlitInfoVec &infos, Program &program, bool scaled);
			void reset_scissor_queue();
			const ClearCandidate *find_clear_candidate(const Rect &rect) const;

			Rect compute_window_rect(const TextureWindow &window);

			ImageHandle last_scanout;
			ImageHandle reuseable_scanout;
			DisplayRect compute_display_rect();

			Rect compute_vram_framebuffer_rect();

			void mipmap_framebuffer();
			void ssaa_framebuffer();
			BufferHandle quad = { NULL };
	};

	static const uint32_t quad_vert[] =
#include "shaders_vulkan/prebuilt/quad.vert.inc"
		;
	static const uint32_t scaled_quad_frag[] =
#include "shaders_vulkan/prebuilt/scaled.quad.frag.inc"
		;
	static const uint32_t scaled_dither_quad_frag[] =
#include "shaders_vulkan/prebuilt/scaled.dither.quad.frag.inc"
		;
	static const uint32_t bpp24_quad_frag[] =
#include "shaders_vulkan/prebuilt/bpp24.quad.frag.inc"
		;
	static const uint32_t bpp24_yuv_quad_frag[] =
#include "shaders_vulkan/prebuilt/bpp24.yuv.quad.frag.inc"
		;
	static const uint32_t unscaled_quad_frag[] =
#include "shaders_vulkan/prebuilt/unscaled.quad.frag.inc"
		;
	static const uint32_t unscaled_dither_quad_frag[] =
#include "shaders_vulkan/prebuilt/unscaled.dither.quad.frag.inc"
		;
	static const uint32_t copy_vram_comp[] =
#include "shaders_vulkan/prebuilt/copy_vram.comp.inc"
		;
	static const uint32_t copy_vram_masked_comp[] =
#include "shaders_vulkan/prebuilt/copy_vram.masked.comp.inc"
		;
	static const uint32_t resolve_to_scaled[] =
#include "shaders_vulkan/prebuilt/resolve.scaled.comp.inc"
		;
	static const uint32_t resolve_to_msaa_scaled[] =
#include "shaders_vulkan/prebuilt/resolve.msaa.scaled.comp.inc"
		;
	static const uint32_t resolve_to_unscaled[] =
#include "shaders_vulkan/prebuilt/resolve.unscaled.comp.inc"
		;
	static const uint32_t resolve_msaa_to_unscaled[] =
#include "shaders_vulkan/prebuilt/resolve.msaa.unscaled.comp.inc"
		;

	static const uint32_t flat_vert[] =
#include "shaders_vulkan/prebuilt/flat.vert.inc"
		;
	static const uint32_t flat_unscaled_vert[] =
#include "shaders_vulkan/prebuilt/flat.unscaled.vert.inc"
		;
	static const uint32_t flat_frag[] =
#include "shaders_vulkan/prebuilt/flat.frag.inc"
		;
	static const uint32_t textured_vert[] =
#include "shaders_vulkan/prebuilt/textured.vert.inc"
		;
	static const uint32_t textured_unscaled_vert[] =
#include "shaders_vulkan/prebuilt/textured.unscaled.vert.inc"
		;
	static const uint32_t textured_frag[] =
#include "shaders_vulkan/prebuilt/textured.frag.inc"
		;
	static const uint32_t textured_unscaled_frag[] =
#include "shaders_vulkan/prebuilt/textured.unscaled.frag.inc"
		;
	static const uint32_t textured_msaa_frag[] =
#include "shaders_vulkan/prebuilt/textured.msaa.frag.inc"
		;
	static const uint32_t textured_msaa_unscaled_frag[] =
#include "shaders_vulkan/prebuilt/textured.msaa.unscaled.frag.inc"
		;

	static const uint32_t blit_vram_scaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.scaled.comp.inc"
		;
	static const uint32_t blit_vram_scaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.masked.scaled.comp.inc"
		;
	static const uint32_t blit_vram_cached_scaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.cached.scaled.comp.inc"
		;
	static const uint32_t blit_vram_cached_scaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.cached.masked.scaled.comp.inc"
		;

	static const uint32_t blit_vram_msaa_scaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.msaa.scaled.comp.inc"
		;
	static const uint32_t blit_vram_msaa_scaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.msaa.masked.scaled.comp.inc"
		;
	static const uint32_t blit_vram_msaa_cached_scaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.msaa.cached.scaled.comp.inc"
		;
	static const uint32_t blit_vram_msaa_cached_scaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.msaa.cached.masked.scaled.comp.inc"
		;

	static const uint32_t blit_vram_unscaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.unscaled.comp.inc"
		;
	static const uint32_t blit_vram_unscaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.masked.unscaled.comp.inc"
		;

	static const uint32_t blit_vram_cached_unscaled_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.cached.unscaled.comp.inc"
		;
	static const uint32_t blit_vram_cached_unscaled_masked_comp[] =
#include "shaders_vulkan/prebuilt/blit_vram.cached.masked.unscaled.comp.inc"
		;

	static const uint32_t feedback_frag[] =
#include "shaders_vulkan/prebuilt/feedback.frag.inc"
		;
	static const uint32_t feedback_unscaled_frag[] =
#include "shaders_vulkan/prebuilt/feedback.unscaled.frag.inc"
		;
	static const uint32_t feedback_flat_frag[] =
#include "shaders_vulkan/prebuilt/feedback.flat.frag.inc"
		;

	static const uint32_t feedback_msaa_frag[] =
#include "shaders_vulkan/prebuilt/feedback.msaa.frag.inc"
		;
	static const uint32_t feedback_msaa_unscaled_frag[] =
#include "shaders_vulkan/prebuilt/feedback.msaa.unscaled.frag.inc"
		;
	static const uint32_t feedback_msaa_flat_frag[] =
#include "shaders_vulkan/prebuilt/feedback.msaa.flat.frag.inc"
		;

	static const uint32_t mipmap_vert[] =
#include "shaders_vulkan/prebuilt/mipmap.vert.inc"
		;
	static const uint32_t mipmap_shifted_vert[] =
#include "shaders_vulkan/prebuilt/mipmap.shifted.vert.inc"
		;
	static const uint32_t mipmap_energy_first_frag[] =
#include "shaders_vulkan/prebuilt/mipmap.energy.first.frag.inc"
		;
	static const uint32_t mipmap_resolve_frag[] =
#include "shaders_vulkan/prebuilt/mipmap.resolve.frag.inc"
		;
	static const uint32_t mipmap_dither_resolve_frag[] =
#include "shaders_vulkan/prebuilt/mipmap.dither.resolve.frag.inc"
		;
	static const uint32_t mipmap_energy_frag[] =
#include "shaders_vulkan/prebuilt/mipmap.energy.frag.inc"
		;
	static const uint32_t mipmap_energy_blur_frag[] =
#include "shaders_vulkan/prebuilt/mipmap.energy.blur.frag.inc"
		;

// 3 LSBs are ignored.
#define FBCOLOR_TO_RGBA8(color) ((color) & 0xfff8f8f8u)

static inline void fbcolor_to_rgba32f(float *v, uint32_t color)
{
	// 3 LSBs are ignored.
	unsigned r = (color >> 0) & 0xf8;
	unsigned g = (color >> 8) & 0xf8;
	unsigned b = (color >> 16) & 0xf8;
	v[0] = r * (1.0f / 255.0f);
	v[1] = g * (1.0f / 255.0f);
	v[2] = b * (1.0f / 255.0f);
	// Mask bit is always cleared.
	v[3] = 0.0f;
}


Renderer::Renderer(Device &device_, unsigned scaling_, unsigned msaa_, const SaveState *state)
    : device(&device_)
    , scaling(scaling_)
    , msaa(msaa_)
{
	/* ImageHandle is now a plain struct with no default member initializer;
	 * null every handle member so conditional assignment and ~Renderer's
	 * ih_reset are safe. */
	cmd.data                     = NULL;
	quad.data                    = NULL;
	scaled_framebuffer.data      = NULL;
	scaled_framebuffer_msaa.data = NULL;
	bias_framebuffer.data        = NULL;
	framebuffer.data             = NULL;
	framebuffer_ssaa.data        = NULL;
	dither_lut.data              = NULL;
	last_scanout.data            = NULL;
	reuseable_scanout.data       = NULL;
	fbatlas_init(&atlas);
	texture_tracker_init(&tracker); /* embedded TextureTracker: explicit init (was default ctor) */
	// Sanity check settings, 16x IR with 16x MSAA will exhaust most GPUs VRAM alone.
	if (scaling == 16 && msaa > 1)
	{
		LOGI("[Vulkan]: Internal resolution scale of 16x is used, limiting MSAA to 1x for memory reasons.\n");
		msaa = 1;
	}
	else if (scaling == 8 && msaa > 4)
	{
		LOGI("[Vulkan]: Internal resolution scale of 8x is used, limiting MSAA to 4x for memory reasons.\n");
		msaa = 4;
	}

	// Verify we can actually render at our target scaling factor.
	// Some devices only support 8K textures, which means max 8x scale.
	VkImageFormatProperties props;
	if (device->get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_STORAGE_BIT |
				VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
				0,
				&props))
	{
		unsigned max_scaling = min_(props.maxExtent.width / FB_WIDTH, props.maxExtent.height / FB_HEIGHT);
		unsigned new_scale = scaling;
		while (new_scale > max_scaling)
			new_scale >>= 1;

		if (new_scale != scaling)
		{
			LOGI("[Vulkan]: Internal resolution scale of %ux was chosen, but this is not supported, using %ux instead.\n",
					scaling, new_scale);
			scaling = new_scale;
		}
	}
	else
	{
		LOGE("[Vulkan]: RGBA8_UNORM is not supported. This should never happen, and something might have been corrupted.\n");
		return;
	}

	ImageCreateInfo info = ImageCreateInfo::render_target(FB_WIDTH, FB_HEIGHT, VK_FORMAT_R32_UINT);
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;

	if (state)
	{
		render_state = state->state;
		fbatlas_set_texture_offset(&atlas, render_state.texture_offset_x, render_state.texture_offset_y);
		fbatlas_set_texture_mode(&atlas, render_state.texture_mode);
		fbatlas_set_draw_rect(&atlas, render_state.draw_rect);
		fbatlas_set_palette_offset(&atlas, render_state.palette_offset_x, render_state.palette_offset_y);
		fbatlas_set_texture_window(&atlas, render_state.cached_window_rect);
		fbatlas_write_transfer(&atlas, Domain_Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	}

	ImageInitialData initial_vram = {
		state ? state->vram.data() : NULL, 0, 0,
	};
	ih_move(&framebuffer, device->create_image(info, state ? &initial_vram : NULL));
	image_set_layout(ih_get(&framebuffer), Layout_General);
	ih_move(&framebuffer_ssaa, device->create_image(info));
	image_set_layout(ih_get(&framebuffer_ssaa), Layout_General);

	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.format = VK_FORMAT_R8_UNORM;
	info.levels = 1;
	ih_move(&bias_framebuffer, device->create_image(info, NULL));

	info.width *= scaling;
	info.height *= scaling;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.levels = trailing_zeroes(scaling) + 1;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
	             VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
	ih_move(&scaled_framebuffer, device->create_image(info));
	image_set_layout(ih_get(&scaled_framebuffer), Layout_General);

	{
		ImageViewCreateInfo view_info = imageview_get_create_info(&image_get_view(ih_get(&scaled_framebuffer)));
		for (unsigned i = 0; i < info.levels; i++)
		{
			view_info.base_level = i;
			view_info.levels = 1;
			scaled_views.push(device->create_image_view(view_info));
		}
	}

	// Check for support.
	if (msaa > 1)
	{
		if (!device->get_device_features().enabled_features.sampleRateShading)
		{
			msaa = 1;
			LOGI("[Vulkan]: sampleRateShading is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device->get_device_features().enabled_features.shaderStorageImageMultisample)
		{
			msaa = 1;
			LOGI("[Vulkan]: shaderStorageImageMultisample is not supported by this implementation. Cannot use MSAA.\n");
		}
		else if (!device->get_image_format_properties(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_STORAGE_BIT |
					VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_SAMPLED_BIT,
					0,
					&props))
		{
			LOGI("[Vulkan]: Cannot use multisampling with this device.\n");
			msaa = 1;
		}
		else if ((msaa & props.sampleCounts) == 0)
		{
			unsigned new_msaa = msaa >> 1;
			while (new_msaa)
			{
				if (new_msaa & props.sampleCounts)
				{
					LOGI("[Vulkan]: MSAA sample count of %u is not supported, falling back to %u.\n",
							msaa, new_msaa);
					msaa = new_msaa;
					break;
				}
			}

			if (msaa == 0)
				msaa = 1;
		}
	}

	if (msaa > 1)
	{
		info.levels = 1;
		info.samples = (VkSampleCountFlagBits)(msaa);
		ih_move(&scaled_framebuffer_msaa, device->create_image(info));
		image_set_layout(ih_get(&scaled_framebuffer_msaa), Layout_General);
		// General layout for MSAA is going to be brutal bandwidth-wise, but we have no real choice.
		// The expectation is that this will be used with a lower scaling factor to compensate.
	}

	fbatlas_set_hazard_listener(&atlas, this);
	texture_tracker_set_texture_uploader(&tracker, this);

	init_pipelines();

	ensure_command_buffer();
	commandbuffer_clear_image(cbh_get(&cmd), *ih_get(&scaled_framebuffer), {});
	if (!state)
		commandbuffer_clear_image(cbh_get(&cmd), *ih_get(&framebuffer), {});
	commandbuffer_full_barrier(cbh_get(&cmd));

	ImageCreateInfo dither_info = ImageCreateInfo::immutable_2d_image(4, 4, VK_FORMAT_R8_UNORM);
	// This lut is biased with 4 to be able to use UNORM easily.
	static const uint8_t dither_lut_data[16] = { 0, 4, 1, 5, 6, 2, 7, 3, 1, 5, 0, 4, 7, 3, 6, 2 };

	ImageInitialData dither_initial = { dither_lut_data };
	ih_move(&dither_lut, device->create_image(dither_info, &dither_initial));

	static const float quad_data[] = {
		-128, -128, +127, -128, -128, +127, +127, +127,
	};

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain_Device;
	buffer_create_info.size = sizeof(quad_data);
	buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	quad = device->create_buffer(buffer_create_info, quad_data);

	flush();
	reset_scissor_queue();

	if (state) {
		texture_tracker_load_state(&tracker, state->tracker_state);
	}

	valid = true;
}

Renderer::SaveState Renderer::save_vram_state()
{
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain_CachedHost;
	buffer_create_info.size = FB_WIDTH * FB_HEIGHT * sizeof(uint32_t);
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	BufferHandle buffer = device->create_buffer(buffer_create_info, NULL);
	fbatlas_read_transfer(&atlas, Domain_Unscaled, { 0, 0, FB_WIDTH, FB_HEIGHT });
	ensure_command_buffer();
	commandbuffer_copy_image_to_buffer(cbh_get(&cmd), *bh_get(&buffer), *ih_get(&framebuffer), 0, { 0, 0, 0 }, { FB_WIDTH, FB_HEIGHT, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });
	commandbuffer_barrier_simple(cbh_get(&cmd), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
	             VK_ACCESS_HOST_READ_BIT);

	flush();

	device->wait_idle();
	const uint32_t *src = (const uint32_t *)(
			device->map_host_buffer(*bh_get(&buffer), MEMORY_ACCESS_READ_BIT));
	/* Deep-copy the mapped VRAM straight into the owning buffer (no
	 * default zero-fill), then move it into the returned SaveState. */
	Renderer::OwnedU32Buf vram;
	vram.assign(src, FB_WIDTH * FB_HEIGHT);
	device->unmap_host_buffer(*bh_get(&buffer), MEMORY_ACCESS_READ_BIT);
	return { static_cast<Renderer::OwnedU32Buf &&>(vram), render_state, texture_tracker_save_state(&tracker) };
}

void Renderer::init_primitive_pipelines()
{
	if (msaa > 1 || scaling > 1)
		pipelines.flat = device->request_program(flat_vert, sizeof(flat_vert), flat_frag, sizeof(flat_frag));
	else
		pipelines.flat = device->request_program(flat_unscaled_vert, sizeof(flat_unscaled_vert), flat_frag, sizeof(flat_frag));

	if (msaa > 1)
	{
		pipelines.textured_scaled = device->request_program(textured_vert, sizeof(textured_vert), textured_msaa_frag, sizeof(textured_msaa_frag));
		pipelines.textured_unscaled = device->request_program(textured_vert, sizeof(textured_vert), textured_msaa_unscaled_frag, sizeof(textured_msaa_unscaled_frag));
	}
	else
	{
		if (scaling > 1)
		{
			pipelines.textured_scaled = device->request_program(textured_vert, sizeof(textured_vert), textured_frag, sizeof(textured_frag));
			pipelines.textured_unscaled = device->request_program(textured_vert, sizeof(textured_vert), textured_unscaled_frag, sizeof(textured_unscaled_frag));
		}
		else
		{
			pipelines.textured_scaled = device->request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert), textured_frag, sizeof(textured_frag));
			pipelines.textured_unscaled = device->request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert), textured_unscaled_frag, sizeof(textured_unscaled_frag));
		}
	}
}

void Renderer::init_primitive_feedback_pipelines()
{
	// TODO: The masked pipelines do not have filter options.
	if (msaa > 1)
	{
		pipelines.textured_masked_scaled = device->request_program(textured_vert, sizeof(textured_vert),
				feedback_msaa_frag, sizeof(feedback_msaa_frag));
		pipelines.textured_masked_unscaled = device->request_program(textured_vert, sizeof(textured_vert),
				feedback_msaa_unscaled_frag, sizeof(feedback_msaa_unscaled_frag));
		pipelines.flat_masked = device->request_program(flat_vert, sizeof(flat_vert),
				feedback_msaa_flat_frag, sizeof(feedback_msaa_flat_frag));
	}
	else
	{
		if (scaling > 1)
		{
			pipelines.flat_masked = device->request_program(flat_vert, sizeof(flat_vert),
					feedback_flat_frag, sizeof(feedback_flat_frag));
			pipelines.textured_masked_scaled = device->request_program(textured_vert, sizeof(textured_vert),
					feedback_frag, sizeof(feedback_frag));
			pipelines.textured_masked_unscaled = device->request_program(textured_vert, sizeof(textured_vert),
					feedback_unscaled_frag, sizeof(feedback_unscaled_frag));
		}
		else
		{
			pipelines.flat_masked = device->request_program(flat_unscaled_vert, sizeof(flat_unscaled_vert),
					feedback_flat_frag, sizeof(feedback_flat_frag));
			pipelines.textured_masked_scaled = device->request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert),
					feedback_frag, sizeof(feedback_frag));
			pipelines.textured_masked_unscaled = device->request_program(textured_unscaled_vert, sizeof(textured_unscaled_vert),
					feedback_unscaled_frag, sizeof(feedback_unscaled_frag));
		}
	}
}

void Renderer::init_pipelines()
{
	if (msaa > 1)
		pipelines.resolve_to_unscaled = device->request_program(resolve_msaa_to_unscaled, sizeof(resolve_msaa_to_unscaled));
	else
		pipelines.resolve_to_unscaled = device->request_program(resolve_to_unscaled, sizeof(resolve_to_unscaled));

	pipelines.scaled_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), scaled_quad_frag, sizeof(scaled_quad_frag));
	pipelines.scaled_dither_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), scaled_dither_quad_frag, sizeof(scaled_dither_quad_frag));
	pipelines.bpp24_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), bpp24_quad_frag, sizeof(bpp24_quad_frag));
	pipelines.bpp24_yuv_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), bpp24_yuv_quad_frag, sizeof(bpp24_yuv_quad_frag));
	pipelines.unscaled_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), unscaled_quad_frag, sizeof(unscaled_quad_frag));
	pipelines.unscaled_dither_quad_blitter =
		device->request_program(quad_vert, sizeof(quad_vert), unscaled_dither_quad_frag, sizeof(unscaled_dither_quad_frag));

	pipelines.copy_to_vram = device->request_program(copy_vram_comp, sizeof(copy_vram_comp));
	pipelines.copy_to_vram_masked = device->request_program(copy_vram_masked_comp, sizeof(copy_vram_masked_comp));

	if (msaa > 1)
	{
		pipelines.resolve_to_scaled =
			device->request_program(resolve_to_msaa_scaled, sizeof(resolve_to_msaa_scaled));

		pipelines.blit_vram_scaled =
			device->request_program(blit_vram_msaa_scaled_comp, sizeof(blit_vram_msaa_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device->request_program(blit_vram_msaa_scaled_masked_comp, sizeof(blit_vram_msaa_scaled_masked_comp));
		pipelines.blit_vram_msaa_cached_scaled =
			device->request_program(blit_vram_msaa_cached_scaled_comp, sizeof(blit_vram_msaa_cached_scaled_comp));
		pipelines.blit_vram_msaa_cached_scaled_masked =
			device->request_program(blit_vram_msaa_cached_scaled_masked_comp, sizeof(blit_vram_msaa_cached_scaled_masked_comp));
	}
	else
	{
		pipelines.resolve_to_scaled = device->request_program(resolve_to_scaled, sizeof(resolve_to_scaled));

		pipelines.blit_vram_scaled = device->request_program(blit_vram_scaled_comp, sizeof(blit_vram_scaled_comp));
		pipelines.blit_vram_scaled_masked =
			device->request_program(blit_vram_scaled_masked_comp, sizeof(blit_vram_scaled_masked_comp));
	}

	pipelines.blit_vram_cached_scaled =
		device->request_program(blit_vram_cached_scaled_comp, sizeof(blit_vram_cached_scaled_comp));
	pipelines.blit_vram_cached_scaled_masked =
		device->request_program(blit_vram_cached_scaled_masked_comp, sizeof(blit_vram_cached_scaled_masked_comp));

	pipelines.blit_vram_unscaled = device->request_program(blit_vram_unscaled_comp, sizeof(blit_vram_unscaled_comp));
	pipelines.blit_vram_unscaled_masked =
		device->request_program(blit_vram_unscaled_masked_comp, sizeof(blit_vram_unscaled_masked_comp));
	pipelines.blit_vram_cached_unscaled =
		device->request_program(blit_vram_cached_unscaled_comp, sizeof(blit_vram_cached_unscaled_comp));
	pipelines.blit_vram_cached_unscaled_masked =
		device->request_program(blit_vram_cached_unscaled_masked_comp, sizeof(blit_vram_cached_unscaled_masked_comp));

	pipelines.mipmap_resolve =
		device->request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_resolve_frag, sizeof(mipmap_resolve_frag));
	pipelines.mipmap_dither_resolve =
		device->request_program(mipmap_vert, sizeof(mipmap_vert), mipmap_dither_resolve_frag, sizeof(mipmap_dither_resolve_frag));

	pipelines.mipmap_energy = device->request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_frag, sizeof(mipmap_energy_frag));
	pipelines.mipmap_energy_first = device->request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_first_frag, sizeof(mipmap_energy_first_frag));
	pipelines.mipmap_energy_blur = device->request_program(mipmap_shifted_vert, sizeof(mipmap_shifted_vert),
			mipmap_energy_blur_frag, sizeof(mipmap_energy_blur_frag));

	init_primitive_pipelines();
	init_primitive_feedback_pipelines();
}

void Renderer::set_draw_rect(const Rect &rect)
{
	fbatlas_set_draw_rect(&atlas, rect);
	render_state.draw_rect = rect;

	const VkRect2D &last = *Rect2DVec_back(&queue.scissors);
	const int scaled_x = int(rect.x * scaling);
	const int scaled_y = int(rect.y * scaling);
	const unsigned scaled_w = rect.width * scaling;
	const unsigned scaled_h = rect.height * scaling;
	if (last.offset.x != scaled_x || last.offset.y != scaled_y ||
	    last.extent.width != scaled_w || last.extent.height != scaled_h)
		{ VkRect2D _vpush = { { scaled_x, scaled_y }, { scaled_w, scaled_h } }; Rect2DVec_push(&queue.scissors, &_vpush); }
}

void Renderer::clear_rect(const Rect &rect, uint32_t fb_color)
{
	if (texture_tracking_enabled) {
		texture_tracker_clearRegion(&tracker, rect);
	}
	ih_reset(&last_scanout);
	fbatlas_clear_rect(&atlas, rect, fb_color);

	VK_ASSERT(rect.x + rect.width <= FB_WIDTH);
	VK_ASSERT(rect.y + rect.height <= FB_HEIGHT);
}

Rect Renderer::compute_window_rect(const TextureWindow &window)
{
	unsigned mask_bits_x = 32 - leading_zeroes(window.mask_x);
	unsigned mask_bits_y = 32 - leading_zeroes(window.mask_y);
	unsigned x = window.or_x & ~((1u << mask_bits_x) - 1);
	unsigned y = window.or_y & ~((1u << mask_bits_y) - 1);
	return { x, y, 1u << mask_bits_x, 1u << mask_bits_y };
}

void Renderer::copy_vram_to_cpu_synchronous(const Rect &rect, uint16_t *vram)
{
	bool wrap_x = rect.x + rect.width > FB_WIDTH;
	bool wrap_y = rect.y + rect.height > FB_HEIGHT;
	Rect copy_rect = rect;
	bool wrap = wrap_x || wrap_y;
	// We could do four separate reads but this is eaiser
	if (wrap)
	{
		copy_rect.x = 0;
		copy_rect.width = FB_WIDTH;
		copy_rect.y = 0;
		copy_rect.height = FB_HEIGHT;
	}

	fbatlas_read_transfer(&atlas, Domain_Unscaled, copy_rect);
	ensure_command_buffer();

	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain_CachedHost;
	buffer_create_info.size = copy_rect.width * copy_rect.height * 4;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	BufferHandle buffer = device->create_buffer(buffer_create_info, NULL);
	commandbuffer_copy_image_to_buffer(cbh_get(&cmd), *bh_get(&buffer), *ih_get(&framebuffer), 0, { int(copy_rect.x), int(copy_rect.y), 0 },
	                          { copy_rect.width, copy_rect.height, 1 }, 0, 0,
	                          { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

	commandbuffer_barrier_simple(cbh_get(&cmd), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
	             VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);

	Fence fence = flush_and_signal();
	fenceholder_wait(fence_get(&fence));

	const uint32_t *mapped = (const uint32_t *)(device->map_host_buffer(*bh_get(&buffer), MEMORY_ACCESS_READ_BIT));

	if (!wrap)
	{
		for (uint16_t y = 0; y < rect.height; y++)
			for (uint16_t x = 0; x < rect.width; x++)
				vram[(y + rect.y) * FB_WIDTH + (x + rect.x)] = uint16_t(mapped[y * rect.width + x]);
	}
	else
	{
		for (uint16_t y = 0; y < rect.height; y++)
			for (uint16_t x = 0; x < rect.width; x++)
				{
					uint32_t h = (x + rect.x) & (FB_WIDTH - 1);
					uint32_t v = (y + rect.y) & (FB_HEIGHT - 1);
					uint32_t p = v * FB_WIDTH + h;
					vram[p] = uint16_t(mapped[p]);
				}
	}

	if (texture_tracking_enabled) {
		texture_tracker_notifyReadback(&tracker, rect, vram);
	}

	device->unmap_host_buffer(*bh_get(&buffer), MEMORY_ACCESS_READ_BIT);

	/* Single surviving owner of the fence handle (ownership moved out of
	 * flush_and_signal); drop its reference at scope exit. */
	fence_reset(&fence);
}

void Renderer::mipmap_framebuffer()
{
	// render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect rect = render_state.display_fb_rect;
	if (rect.x + rect.width > FB_WIDTH)
	{
		rect.x = 0;
		rect.width = FB_WIDTH;
	}
	if (rect.y + rect.height > FB_HEIGHT)
	{
		rect.y = 0;
		rect.height = FB_HEIGHT;
	}
	unsigned levels = scaled_views.size();

	ensure_command_buffer();
	for (unsigned i = 1; i <= levels; i++)
	{
		RenderPassInfo rp;
		unsigned current_scale = max_(scaling >> i, 1u);

		if (i == levels)
			rp.color_attachments[0] = &image_get_view(ih_get(&bias_framebuffer));
		else
			rp.color_attachments[0] = iv_get(&scaled_views[i]);

		rp.num_color_attachments = 1;
		rp.store_attachments = 1;
		rp.render_area = { { int(rect.x * current_scale), int(rect.y * current_scale) },
			               { rect.width * current_scale, rect.height * current_scale } };

		if (i == levels)
		{
			commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&bias_framebuffer), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
		}

		commandbuffer_begin_render_pass(cbh_get(&cmd), rp);

		if (i == levels)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.mipmap_energy_blur);
		else if (i == 1)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.mipmap_energy_first);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.mipmap_energy);

		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[i - 1]), StockSampler_LinearClamp);

		commandbuffer_set_quad_state(cbh_get(&cmd));
		commandbuffer_set_vertex_binding(cbh_get(&cmd), 0, *bh_get(&quad), 0, 8);
		struct Push
		{
			float offset[2];
			float range[2];
			float inv_resolution[2];
			float uv_min[2];
			float uv_max[2];
		};
		Push push = {
			{ float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
			{ float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
			{ 1.0f / (FB_WIDTH * current_scale), 1.0f / (FB_HEIGHT * current_scale) },
			{ (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
			{ (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		};
		commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
		commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
		commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
		commandbuffer_draw(cbh_get(&cmd), 4);

		commandbuffer_end_render_pass(cbh_get(&cmd));

		if (i == levels)
		{
			commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&bias_framebuffer), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
		else
		{
			commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&scaled_framebuffer), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			                   VK_ACCESS_SHADER_READ_BIT);
		}
	}
}

void Renderer::ssaa_framebuffer()
{
	// render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect &rect = render_state.display_fb_rect;
	unsigned left = rect.x / BLOCK_WIDTH;
	unsigned top = rect.y / BLOCK_HEIGHT;
	unsigned right = (rect.x + rect.width + BLOCK_WIDTH - 1) / BLOCK_WIDTH;
	unsigned bottom = (rect.y + rect.height + BLOCK_HEIGHT - 1) / BLOCK_HEIGHT;

	/* Exact element count is known up front (block grid), so use a single
	 * malloc'd buffer filled by index rather than a growing std::vector. */
	unsigned resolves_count = (bottom > top && right > left) ? (bottom - top) * (right - left) : 0;
	VkRect2D *resolves_ssaa = (VkRect2D *)malloc((resolves_count ? resolves_count : 1) * sizeof(VkRect2D));
	unsigned resolves_n = 0;
	for (unsigned y = top; y < bottom; y++)
		for (unsigned x = left; x < right; x++)
		{
			VkRect2D r = {
				{ int(x * BLOCK_WIDTH % FB_WIDTH), int(y * BLOCK_HEIGHT % FB_HEIGHT) },
				{ BLOCK_WIDTH, BLOCK_HEIGHT }
			};
			resolves_ssaa[resolves_n++] = r;
		}

	ensure_command_buffer();

	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Samples, msaa);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_FilterMode, 1);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Scaling, scaling);
	commandbuffer_set_program(cbh_get(&cmd), *pipelines.resolve_to_unscaled);
	commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer_ssaa)));
	if (msaa > 1)
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&scaled_framebuffer_msaa)), StockSampler_NearestClamp);
	else
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, *iv_get(&scaled_views[0]), StockSampler_LinearClamp);

	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};
	unsigned size = resolves_n;
	for (unsigned i = 0; i < size; i += 1024)
	{
		unsigned to_run = min_(size - i, 1024u);

		Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
		commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
		void *ptr = commandbuffer_allocate_constant_data(cbh_get(&cmd), 1, 0, to_run * sizeof(VkRect2D));
		memcpy(ptr, resolves_ssaa + i, to_run * sizeof(VkRect2D));
		commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);
		commandbuffer_dispatch(cbh_get(&cmd), 1, 1, to_run);
	}
	free(resolves_ssaa);
}

Rect Renderer::compute_vram_framebuffer_rect()
{
	unsigned clock_div;
	switch (render_state.width_mode)
	{
	case WidthMode_WIDTH_MODE_256:
		clock_div = 10;
		break;
	case WidthMode_WIDTH_MODE_320:
		clock_div = 8;
		break;
	case WidthMode_WIDTH_MODE_512:
		clock_div = 5;
		break;
	case WidthMode_WIDTH_MODE_640:
		clock_div = 4;
		break;
	case WidthMode_WIDTH_MODE_368:
		clock_div = 7;
		break;
	}

	unsigned fb_width = (unsigned) (render_state.horiz_end - render_state.horiz_start);
	fb_width /= clock_div;
	fb_width = (fb_width + 2) & ~3;

	unsigned fb_height = (unsigned) (render_state.vert_end - render_state.vert_start);
	fb_height *= render_state.is_480i ? 2 : 1;

	return {render_state.display_fb_xstart,
	        render_state.display_fb_ystart,
	        fb_width,
	        fb_height};
}

DisplayRect Renderer::compute_display_rect()
{
	unsigned clock_div;
	switch (render_state.width_mode)
	{
	case WidthMode_WIDTH_MODE_256:
		clock_div = 10;
		break;
	case WidthMode_WIDTH_MODE_320:
		clock_div = 8;
		break;
	case WidthMode_WIDTH_MODE_512:
		clock_div = 5;
		break;
	case WidthMode_WIDTH_MODE_640:
		clock_div = 4;
		break;
	case WidthMode_WIDTH_MODE_368:
		clock_div = 7;
		break;
	}

	unsigned display_width;
	int left_offset;
	if (render_state.crop_overscan)
	{
		// Horizontal crop amount is currently hardcoded. Future improvement could allow adjusting this.
		// Restore old center behaviour is render_state.horiz_start is intentionally very high.
		// 938 fixes Gunbird (1008) and Mobile Light Force (EU release of Gunbird),
		// but this value should be lowerer in the future if necessary.
		display_width = (2560/clock_div) - render_state.image_crop;
		if ((render_state.horiz_start < 938) && (render_state.crop_overscan == 2))
			left_offset = floor((render_state.offset_cycles / (double) clock_div) - (render_state.image_crop / 2));
		else
			left_offset = floor(((render_state.horiz_start + render_state.offset_cycles - 608) / (double) clock_div) - (render_state.image_crop / 2));
	}
	else
	{
		display_width = 2800/clock_div;
		left_offset = floor((render_state.horiz_start + render_state.offset_cycles - 488) / (double) clock_div);
	}

	unsigned display_height;
	int upper_offset;
	if (render_state.crop_overscan == 2)
	{
		if (render_state.is_pal)
		{
			display_height = (render_state.vert_end - render_state.vert_start) - (287 - render_state.slend_pal) - render_state.slstart_pal;
			upper_offset = 0 - render_state.slstart_pal;
		}
		else
		{
			display_height = (render_state.vert_end - render_state.vert_start) - (239 - render_state.slend) - render_state.slstart;
			upper_offset = 0 - render_state.slstart;
		}
	}
	if (render_state.crop_overscan != 2 || display_height > (render_state.is_pal ? 288 : 240))
	{
		if (render_state.is_pal)
		{
			display_height = render_state.slend_pal - render_state.slstart_pal + 1;
			upper_offset = render_state.vert_start - 20 - render_state.slstart_pal;
		}
		else
		{
			display_height = render_state.slend - render_state.slstart + 1;
			upper_offset = render_state.vert_start - 16 - render_state.slstart;
		}
	}
	if (render_state.is_480i)
	{
		display_height *= 2;
		upper_offset   *= 2;
	}

	return display_rect_make(left_offset, upper_offset, display_width, display_height);
}

ImageHandle Renderer::scanout_vram_to_texture(bool scaled)
{
	// Like scanout_to_texture(), but synchronizes the entire
	// VRAM framebuffer atlas before scanout. Does not apply
	// any scanout filters and currently outputs at 15-bit
	// color depth. Current implementation does not reuse
	// prior scanouts.

	fbatlas_flush_render_pass(&atlas);

	Rect vram_rect = {0, 0, FB_WIDTH, FB_HEIGHT};

	if (scaled)
		fbatlas_read_fragment(&atlas, Domain_Scaled, vram_rect);
	else
		fbatlas_read_fragment(&atlas, Domain_Unscaled, vram_rect);

	ensure_command_buffer();

	if (scaled && msaa > 1)
	{
		VkImageSubresourceLayers subres = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		VkOffset3D offset = { 0, 0, 0 };
		VkExtent3D extent = { FB_WIDTH * scaling, FB_HEIGHT * scaling, 1 };
		VkImageResolve region = { subres, offset, subres, offset, extent };
		vkCmdResolveImage(commandbuffer_get_command_buffer(cbh_get(&cmd)),
			image_get_image(ih_get(&scaled_framebuffer_msaa)), VK_IMAGE_LAYOUT_GENERAL,
			image_get_image(ih_get(&scaled_framebuffer)), VK_IMAGE_LAYOUT_GENERAL,
			1, &region);

		commandbuffer_barrier_simple(cbh_get(&cmd), 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	unsigned render_scale = scaled ? scaling : 1;

	ImageCreateInfo info = ImageCreateInfo::render_target(
			FB_WIDTH * render_scale,
			FB_HEIGHT * render_scale,
			VK_FORMAT_A1R5G5B5_UNORM_PACK16); // Default to 15bit color for now

	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ih_move(&reuseable_scanout, device->create_image(info));

	RenderPassInfo rp;
	rp.color_attachments[0] = &image_get_view(ih_get(&reuseable_scanout));
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	rp.clear_color[0] = {0, 0, 0, 0};
	rp.clear_attachments = 1;

	commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	commandbuffer_begin_render_pass(cbh_get(&cmd), rp);
	commandbuffer_set_quad_state(cbh_get(&cmd));

	if (scaled)
	{
		commandbuffer_set_program(cbh_get(&cmd), *pipelines.scaled_quad_blitter);
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]), StockSampler_LinearClamp);
	}
	else
	{
		commandbuffer_set_program(cbh_get(&cmd), *pipelines.unscaled_quad_blitter);
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)), StockSampler_LinearClamp);
	}

	commandbuffer_set_vertex_binding(cbh_get(&cmd), 0, *bh_get(&quad), 0, 8);
	struct Push
	{
		float offset[2];
		float scale[2];
		float uv_min[2];
		float uv_max[2];
		float max_bias;
	};

	Push push = { { float(vram_rect.x) / FB_WIDTH, float(vram_rect.y) / FB_HEIGHT },
		          { float(vram_rect.width) / FB_WIDTH, float(vram_rect.height) / FB_HEIGHT },
		          { (vram_rect.x + 0.5f) / FB_WIDTH, (vram_rect.y + 0.5f) / FB_HEIGHT },
		          { (vram_rect.x + vram_rect.width - 0.5f) / FB_WIDTH, (vram_rect.y + vram_rect.height - 0.5f) / FB_HEIGHT },
		          float(scaled_views.size() - 1) };

	commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	commandbuffer_draw(cbh_get(&cmd), 4);

	commandbuffer_end_render_pass(cbh_get(&cmd));

	commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_SHADER_READ_BIT);

	ih_assign(&last_scanout, &reuseable_scanout);

	return reuseable_scanout;
}

ImageHandle Renderer::scanout_to_texture()
{
	fbatlas_flush_render_pass(&atlas);
	if (texture_tracking_enabled) {
		texture_tracker_endFrame(&tracker);
	}

	if (ih_is_valid(&last_scanout))
		return last_scanout;

	render_state.display_fb_rect = compute_vram_framebuffer_rect();
	Rect &rect = render_state.display_fb_rect;

	if (rect.width == 0 || rect.height == 0 || !render_state.display_on)
	{
		// Black screen, just flush out everything.
		fbatlas_read_fragment(&atlas, Domain_Scaled, { 0, 0, FB_WIDTH, FB_HEIGHT });

		ensure_command_buffer();

		ImageCreateInfo info = ImageCreateInfo::render_target(64u, 64u, VK_FORMAT_R8G8B8A8_UNORM);

		info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		info.usage =
		    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		ih_move(&reuseable_scanout, device->create_image(info));

		RenderPassInfo rp;
		rp.color_attachments[0] = &image_get_view(ih_get(&reuseable_scanout));
		rp.num_color_attachments = 1;
		rp.clear_attachments = 1;
		rp.store_attachments = 1;

		commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		commandbuffer_begin_render_pass(cbh_get(&cmd), rp);
		commandbuffer_end_render_pass(cbh_get(&cmd));

		commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                   VK_ACCESS_SHADER_READ_BIT);

		ih_assign(&last_scanout, &reuseable_scanout);
		return reuseable_scanout;
	}

	bool bpp24 = render_state.scanout_mode == ScanoutMode_BGR24;
	bool ssaa = render_state.scanout_filter == ScanoutFilter_SSAA && scaling != 1;

	Rect read_rect = rect;
	if (rect.x + rect.width > FB_WIDTH)
	{
		read_rect.x = 0;
		read_rect.width = FB_WIDTH;
	}
	if (rect.y + rect.height > FB_HEIGHT)
	{
		read_rect.y = 0;
		read_rect.height = FB_HEIGHT;
	}
	if (bpp24)
	{
		Rect tmp = read_rect;
		if (bpp24)
		{
			tmp.width = (tmp.width * 3 + 1) / 2;
			tmp.width = min_(tmp.width, FB_WIDTH - tmp.x);
		}
		fbatlas_read_fragment(&atlas, Domain_Unscaled, tmp);
	}
	else if (ssaa)
		fbatlas_read_compute(&atlas, Domain_Scaled, read_rect);
	else
		fbatlas_read_fragment(&atlas, Domain_Scaled, read_rect);

	if (!bpp24 && ssaa)
		ssaa_framebuffer();
	else if (msaa > 1)
	{
		ensure_command_buffer();
		VkImageSubresourceLayers subres = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		VkOffset3D offset = { int(rect.x * scaling), int(rect.y * scaling), 0 };
		VkExtent3D extent = { rect.width * scaling, rect.height * scaling, 1 };
		if (rect.x + rect.width > FB_WIDTH)
		{
			offset.x = 0;
			extent.width = FB_WIDTH * scaling;
		}
		if (rect.y + rect.height > FB_HEIGHT)
		{
			offset.y = 0;
			extent.height = FB_HEIGHT * scaling;
		}
		VkImageResolve region = { subres, offset, subres, offset, extent };
		vkCmdResolveImage(commandbuffer_get_command_buffer(cbh_get(&cmd)),
			image_get_image(ih_get(&scaled_framebuffer_msaa)), VK_IMAGE_LAYOUT_GENERAL,
			image_get_image(ih_get(&scaled_framebuffer)), VK_IMAGE_LAYOUT_GENERAL,
			1, &region);

		commandbuffer_barrier_simple(cbh_get(&cmd), 
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	if (render_state.adaptive_smoothing && !bpp24 && !ssaa && scaling != 1)
		mipmap_framebuffer();

	ensure_command_buffer();

	bool scaled = !ssaa;

	unsigned render_scale = scaled ? scaling : 1;

	DisplayRect display_rect = compute_display_rect();

	ImageCreateInfo info = ImageCreateInfo::render_target(
			display_rect.width * render_scale,
			display_rect.height * render_scale,
			render_state.scanout_mode == ScanoutMode_ABGR1555_Dither ? VK_FORMAT_A1R5G5B5_UNORM_PACK16 : VK_FORMAT_R8G8B8A8_UNORM);

	info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ih_move(&reuseable_scanout, device->create_image(info));

	RenderPassInfo rp;
	rp.color_attachments[0] = &image_get_view(ih_get(&reuseable_scanout));
	rp.num_color_attachments = 1;
	rp.store_attachments = 1;

	rp.clear_color[0] = {0, 0, 0, 0};
	//rp.clear_color[0] = {60.0f/256.0f, 230.0f/256.0f, 60.0f/256.0f, 0};
	rp.clear_attachments = 1;

	commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	commandbuffer_begin_render_pass(cbh_get(&cmd), rp);
	commandbuffer_set_quad_state(cbh_get(&cmd));

	VkViewport old_vp = commandbuffer_get_viewport(cbh_get(&cmd));
	VkViewport new_vp = {display_rect.x * (float) render_scale,
	                     display_rect.y * (float) render_scale,
	                     rect.width * (float) render_scale,
	                     rect.height * (float) render_scale,
	                     old_vp.minDepth,
	                     old_vp.maxDepth};

	commandbuffer_set_viewport(cbh_get(&cmd), new_vp);

	bool dither = render_state.scanout_mode == ScanoutMode_ABGR1555_Dither;

	if (bpp24)
	{
		if (render_state.scanout_mdec_filter == ScanoutFilter_MDEC_YUV)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.bpp24_yuv_quad_blitter);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.bpp24_quad_blitter);
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)), StockSampler_NearestWrap);
	}
	else if (ssaa)
	{
		if (dither)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.unscaled_dither_quad_blitter);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.unscaled_quad_blitter);

		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer_ssaa)), StockSampler_NearestWrap);
	}
	else if (!render_state.adaptive_smoothing || scaling == 1)
	{
		if (dither)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.scaled_dither_quad_blitter);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.scaled_quad_blitter);

		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]), StockSampler_LinearWrap);
	}
	else
	{
		if (dither)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.mipmap_dither_resolve);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.mipmap_resolve);

		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer)), StockSampler_TrilinearWrap);
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&bias_framebuffer)), StockSampler_LinearWrap);
	}

	if (dither)
	{
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 2, image_get_view(ih_get(&dither_lut)), StockSampler_NearestWrap);
		struct DitherData
		{
			float range;
			float inv_range;
			float dither_scale;
			int32_t dither_shift;
		};
		DitherData *dither = (DitherData *)commandbuffer_allocate_constant_data(cbh_get(&cmd), 0, 3, 1 * sizeof(DitherData));
		dither->range = 31.0f;
		dither->inv_range = 1.0f / 31.0f;
		dither->dither_scale = 1.0f;

		if (render_state.dither_native_resolution && scaled)
		{
			int32_t shift = 0;
			unsigned tmp = scaling >> 1;
			while (tmp)
			{
				shift++;
				tmp >>= 1;
			}
			dither->dither_shift = shift;
		}
		else
		{
			dither->dither_shift = 0;
		}
	}

	commandbuffer_set_vertex_binding(cbh_get(&cmd), 0, *bh_get(&quad), 0, 8);
	struct Push
	{
		float offset[2];
		float scale[2];
		float uv_min[2];
		float uv_max[2];
		float max_bias;
	};
	Push push = { { float(rect.x) / FB_WIDTH, float(rect.y) / FB_HEIGHT },
		          { float(rect.width) / FB_WIDTH, float(rect.height) / FB_HEIGHT },
		          { (rect.x + 0.5f) / FB_WIDTH, (rect.y + 0.5f) / FB_HEIGHT },
		          { (rect.x + rect.width - 0.5f) / FB_WIDTH, (rect.y + rect.height - 0.5f) / FB_HEIGHT },
		          float(scaled_views.size() - 1) };

	commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	commandbuffer_draw(cbh_get(&cmd), 4);

	commandbuffer_end_render_pass(cbh_get(&cmd));

	commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&reuseable_scanout), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
	                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                   VK_ACCESS_SHADER_READ_BIT);

	ih_assign(&last_scanout, &reuseable_scanout);

	return reuseable_scanout;
}

void Renderer::hazard(StatusFlags flags)
{
	VkPipelineStageFlags src_stages = 0;
	VkAccessFlags src_access = 0;
	VkPipelineStageFlags dst_stages = 0;
	VkAccessFlags dst_access = 0;

	if (flags & (STATUS_FRAGMENT_FB_READ | STATUS_FRAGMENT_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (flags & (STATUS_FRAGMENT_FB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
	}

	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (flags & (STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		src_access |= VK_ACCESS_SHADER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	if (flags & (STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ))
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (flags & (STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE))
	{
		src_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		src_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
		              VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Invalidate render target caches.
	if (flags & (STATUS_TRANSFER_SFB_WRITE | STATUS_COMPUTE_SFB_WRITE | STATUS_FRAGMENT_SFB_WRITE))
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dst_access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		              VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	}

	// 24-bpp scanout hazard
	if (flags & STATUS_COMPUTE_FB_WRITE)
	{
		dst_stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dst_access |= VK_ACCESS_SHADER_READ_BIT;
	}

	dst_stages |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;

	// If we have out-standing jobs in the compute pipe, issue them into cmdbuffer before injecting the barrier.
	if (flags & (STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_READ | STATUS_COMPUTE_SFB_WRITE))
	{
		flush_blits();
		flush_resolves();
	}

	VK_ASSERT(src_stages);
	VK_ASSERT(dst_stages);
	ensure_command_buffer();
	commandbuffer_barrier_simple(cbh_get(&cmd), src_stages, src_access, dst_stages, dst_access);
}

void Renderer::flush_resolves()
{
	struct Push
	{
		float inv_size[2];
		uint32_t scale;
	};

	if (!Rect2DVec_empty(&queue.scaled_resolves))
	{
		ensure_command_buffer();
		commandbuffer_set_program(cbh_get(&cmd), *pipelines.resolve_to_scaled);

		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&framebuffer)), StockSampler_NearestClamp);
		if (msaa > 1)
			commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer_msaa)));
		else
			commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]));

		unsigned size = Rect2DVec_size(&queue.scaled_resolves);
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min_(size - i, 1024u);

			Push push = { { 1.0f / (scaling * FB_WIDTH), 1.0f / (scaling * FB_HEIGHT) }, scaling };
			commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
			void *ptr = commandbuffer_allocate_constant_data(cbh_get(&cmd), 1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, Rect2DVec_data(&queue.scaled_resolves) + i, to_run * sizeof(VkRect2D));
			commandbuffer_dispatch(cbh_get(&cmd), scaling, scaling, to_run);
		}
	}

	if (!Rect2DVec_empty(&queue.unscaled_resolves))
	{
		ensure_command_buffer();
		// Always use nearest neighbor downscaling to avoid filter artifact (e.g. unwanted black outlines in Vagrant Story)
		// Supersampling will use a separate pass for downsampling
		commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Samples, msaa);
		commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_FilterMode, 0);
		commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Scaling, scaling);
		commandbuffer_set_program(cbh_get(&cmd), *pipelines.resolve_to_unscaled);
		commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)));
		if (msaa > 1)
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&scaled_framebuffer_msaa)), StockSampler_NearestClamp);
		else
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, *iv_get(&scaled_views[0]), StockSampler_NearestClamp);

		unsigned size = Rect2DVec_size(&queue.unscaled_resolves);
		for (unsigned i = 0; i < size; i += 1024)
		{
			unsigned to_run = min_(size - i, 1024u);

			Push push = { { 1.0f / FB_WIDTH, 1.0f / FB_HEIGHT }, 1u };
			commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
			void *ptr = commandbuffer_allocate_constant_data(cbh_get(&cmd), 1, 0, to_run * sizeof(VkRect2D));
			memcpy(ptr, Rect2DVec_data(&queue.unscaled_resolves) + i, to_run * sizeof(VkRect2D));
			commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);
			commandbuffer_dispatch(cbh_get(&cmd), 1, 1, to_run);
		}
	}

	Rect2DVec_clear(&queue.scaled_resolves);
	Rect2DVec_clear(&queue.unscaled_resolves);
}

void Renderer::resolve(Domain target_domain, unsigned x, unsigned y)
{
	if (target_domain == Domain_Scaled)
		{ VkRect2D _vpush = { { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } }; Rect2DVec_push(&queue.scaled_resolves, &_vpush); }
	else
		{ VkRect2D _vpush = { { int(x), int(y) }, { BLOCK_WIDTH, BLOCK_HEIGHT } }; Rect2DVec_push(&queue.unscaled_resolves, &_vpush); }
}

void Renderer::ensure_command_buffer()
{
	if (!cbh_is_valid(&cmd))
		cmd = device->request_command_buffer();
}

float Renderer::allocate_depth(Domain domain, const Rect &rect)
{
	fbatlas_write_fragment(&atlas, domain, rect);
	primitive_index++;
	return 1.0f - primitive_index * (4.0f / 0xffffff); // Double the epsilon to be safe(r) when w is used.
	//iCB: Doubled again for added safety, otherwise we get Z-fighting when drawing multi-pass blended primitives.
}

HdTextureHandle Renderer::get_hd_texture_index(const Rect &vram_rect, bool &fastpath_capable_out, bool &cache_hit_out) {
	UsedMode mode = {
		render_state.texture_mode,
		render_state.palette_offset_x,
		render_state.palette_offset_y
	};
	if (mode.mode == TextureMode_ABGR1555) {
		// HACK: This mode doesn't use a palette, so this a hack to make the palette irrelevant for equality purposes
		mode.palette_offset_x = 0;
		mode.palette_offset_y = 0;
	}
	if (texture_tracking_enabled) {
		return texture_tracker_get_hd_texture_index(&tracker, vram_rect, mode, render_state.texture_offset_x, render_state.texture_offset_y, fastpath_capable_out, cache_hit_out);
	} else {
		return HdTextureHandle::make_none();
	}
}

void Renderer::build_attribs(BufferVertex *output, const Vertex *vertices, unsigned count, HdTextureHandle &hd_texture_index,
	bool &filtering, bool &scaled_read, unsigned &shift, bool &offset_uv)
{
	switch (render_state.texture_mode)
	{
	case TextureMode_Palette4bpp:
		shift = 2;
		break;
	case TextureMode_Palette8bpp:
		shift = 1;
		break;
	default:
		shift = 0;
		break;
	}

	Rect hd_texture_vram(0, 0, 0, 0);

	if (render_state.texture_mode != TextureMode_None)
	{
		if (render_state.texture_window.mask_x == 0xffu && render_state.texture_window.mask_y == 0xffu)
		{
			unsigned min_u = render_state.UVLimits.min_u;
			unsigned min_v = render_state.UVLimits.min_v;
			unsigned max_u = render_state.UVLimits.max_u;
			unsigned max_v = render_state.UVLimits.max_v;
			unsigned width = max_u - min_u + 1;
			unsigned height = max_v - min_v + 1;

			if (max_u > 255 || max_v > 255) // Wraparound behavior, assume the whole page is hit.
			{
				fbatlas_set_texture_window(&atlas, { 0, 0, 256u >> shift, 256 });
				hd_texture_vram = {
					render_state.texture_offset_x,
					render_state.texture_offset_y,
					256u >> shift,
					256,
				};
			}
			else
			{
				min_u >>= shift;
				max_u = (max_u + (1 << shift) - 1) >> shift;
				width = max_u - min_u + 1;
				fbatlas_set_texture_window(&atlas, { min_u, min_v, width, height });

				hd_texture_vram = {
					render_state.texture_offset_x + min_u,
					render_state.texture_offset_y + min_v,

					// HDTODO: this might be wrong because it can result in Rect's with 0 width, also notice that height has the same +1
					width - 1, // This is -1 due to boundary shenanigans above (otherwise upload.rect.contains(snoop) would return false for the right-most tiles)
					
					height,
				};
			}
		}
		else
		{
			// If we have a masked texture window, assume this is the true rect we should use.
			Rect effective_rect = render_state.cached_window_rect;
			fbatlas_set_texture_window(&atlas, 
			    { effective_rect.x >> shift, effective_rect.y, effective_rect.width >> shift, effective_rect.height });
			hd_texture_vram = {
				render_state.texture_offset_x + (effective_rect.x >> shift),
				render_state.texture_offset_y + effective_rect.y,
				effective_rect.width >> shift,
				effective_rect.height,
			};
		}
	}

	// Compute bounding box for the draw call.
	float max_x = 0.0f;
	float max_y = 0.0f;
	float min_x = float(FB_WIDTH);
	float min_y = float(FB_HEIGHT);
	float x[4];
	float y[4];
	// Bounding box for texture
	unsigned max_u = 0.0f;
	unsigned max_v = 0.0f;
	unsigned min_u = FB_WIDTH;
	unsigned min_v = FB_HEIGHT;
	for (unsigned i = 0; i < count; i++)
	{
		float tmp_x = vertices[i].x + render_state.draw_offset_x;
		float tmp_y = vertices[i].y + render_state.draw_offset_y;
		max_x = max_(max_x, tmp_x);
		max_y = max_(max_y, tmp_y);
		min_x = min_(min_x, tmp_x);
		min_y = min_(min_y, tmp_y);
		x[i] = tmp_x;
		y[i] = tmp_y;

		if (render_state.texture_mode == TextureMode_ABGR1555)
		{
			unsigned tmp_u = vertices[i].u + render_state.texture_offset_x;
			unsigned tmp_v = vertices[i].v + render_state.texture_offset_y;
			max_u = max_(max_u, tmp_u);
			max_v = max_(max_v, tmp_v);
			min_u = min_(min_u, tmp_u);
			min_v = min_(min_v, tmp_v);
		}
	}

	// Clamp the rect.
	min_x = floorf(max_(min_x, 0.0f));
	min_y = floorf(max_(min_y, 0.0f));
	max_x = ceilf(min_(max_x, float(FB_WIDTH)));
	max_y = ceilf(min_(max_y, float(FB_HEIGHT)));

	const Rect rect = {
		unsigned(min_x), unsigned(min_y), unsigned(max_x) - unsigned(min_x), unsigned(max_y) - unsigned(min_y),
	};

	if (render_state.texture_mode == TextureMode_ABGR1555)
	{
		if (render_state.draw_rect.intersects(rect))
		{
			// HACK hd_texture_vram should contains the texture we are reading from in vram coordinate
			// avoid texture filtering and enable scaled read if the texture is rendered content
			bool texture_rendered = fbatlas_texture_rendered(&atlas, hd_texture_vram);
			filtering = !texture_rendered;
			scaled_read = texture_rendered;
		}
		else
		{
			filtering = false;
			scaled_read = false;
		}
	}
	else
	{
		filtering = render_state.texture_mode != TextureMode_None;
		scaled_read = false;
	}
	offset_uv = scaled_uv_offset && render_state.primitive_type == PrimitiveType_Polygon;

	float z = allocate_depth(scaled_read ? Domain_Scaled : Domain_Unscaled, rect);

	// Look up the hd texture index
	// This is done here at the end of the function because the `allocate_depth`
	// call above can call `reset_queue` which would invalidate the HdTextureHandle
	int16_t param = int16_t(shift);
	if (hd_texture_vram.height > 0) { // This condition is just a dumb way to check that the rect was actually set to something
		bool fastpath_capable_out = false;
		bool cache_hit = false;
		hd_texture_index = get_hd_texture_index(hd_texture_vram, fastpath_capable_out, cache_hit);
		fastpath_capable_out = false;
		if (
			fastpath_capable_out &&
			render_state.texture_window.mask_x == 0xff && render_state.texture_window.mask_y == 0xff &&
			render_state.texture_window.or_x == 0x00 && render_state.texture_window.or_y == 0x00
		) {
			// All UVs are within a single hd texture, and there are no & or | shenanigans. Tell the shader to use the fast path.
			param = param | 0x100;
		}
		if (cache_hit) {
			param = param | 0x400; // dbg cache hit
		}
	}
	if (hd_texture_index == HdTextureHandle::make_none()) {
		// This flag says skip hd textures
		param = param | 0x200;
	}

	for (unsigned i = 0; i < count; i++)
	{
		output[i] = {
			x[i],
			y[i],
			z,
			vertices[i].w,
			vertices[i].color & 0xffffffu,
			render_state.texture_window,
			int16_t(render_state.palette_offset_x),
			int16_t(render_state.palette_offset_y),
			param,
			int16_t(vertices[i].u),
			int16_t(vertices[i].v),
			int16_t(render_state.texture_offset_x),
			int16_t(render_state.texture_offset_y),
			render_state.UVLimits.min_u,
			render_state.UVLimits.min_v,
			render_state.UVLimits.max_u,
			render_state.UVLimits.max_v,
		};

		if (render_state.texture_mode != TextureMode_None && !render_state.texture_color_modulate)
			output[i].color = 0x808080;

		output[i].color |= render_state.force_mask_bit ? 0xff000000u : 0u;
	}
}

Renderer::BufferVertexVec *Renderer::select_pipeline(unsigned prims, int scissor, HdTextureHandle hd_texture,
	bool filtering, bool scaled_read, unsigned shift, bool offset_uv)
{
	// For mask testing, force primitives through the serialized blend path.
	if (render_state.mask_test)
		return NULL;

	if (filtering)
		filtering = !get_filer_exclude(FilterExcludeOpaque);

	if (render_state.texture_mode != TextureMode_None)
	{
		if (render_state.semi_transparent != SemiTransparentMode_None)
		{
			for (unsigned i = 0; i < prims; i++)
			{
				PrimitiveInfo _pi = PrimitiveInfo(PrimitiveInfoVec_size(&queue.semi_transparent_opaque_scissor), scissor, hd_texture,
					filtering, scaled_read, shift, offset_uv);
				PrimitiveInfoVec_push(&queue.semi_transparent_opaque_scissor, &_pi);
			}
			return &queue.semi_transparent_opaque;
		}
		else
		{
			for (unsigned i = 0; i < prims; i++)
			{
				PrimitiveInfo _pi = PrimitiveInfo(PrimitiveInfoVec_size(&queue.opaque_textured_scissor), scissor, hd_texture,
					filtering, scaled_read, shift, offset_uv);
				PrimitiveInfoVec_push(&queue.opaque_textured_scissor, &_pi);
			}
			return &queue.opaque_textured;
		}
	}
	else if (render_state.semi_transparent != SemiTransparentMode_None)
		return NULL;
	else
	{
		for (unsigned i = 0; i < prims; i++)
		{
			PrimitiveInfo _pi = PrimitiveInfo(PrimitiveInfoVec_size(&queue.opaque_scissor), scissor, hd_texture,
				filtering, scaled_read, shift, offset_uv);
			PrimitiveInfoVec_push(&queue.opaque_scissor, &_pi);
		}
		return &queue.opaque;
	}
}

void Renderer::build_line_quad(Vertex *output, const Vertex *input)
{
	const float dx = input[1].x - input[0].x;
	const float dy = input[1].y - input[0].y;
	if (dx == 0.0f && dy == 0.0f)
	{
		// Degenerate, render a point.
		output[0].x = input[0].x;
		output[0].y = input[0].y;
		output[1].x = input[0].x + 1.0f;
		output[1].y = input[0].y;
		output[2].x = input[1].x;
		output[2].y = input[1].y + 1.0f;
		output[3].x = input[1].x + 1.0f;
		output[3].y = input[1].y + 1.0f;

		uint32_t c = input[0].color;
		output[0].w = 1.0f;
		output[0].color = c;
		output[1].w = 1.0f;
		output[1].color = c;
		output[2].w = 1.0f;
		output[2].color = c;
		output[3].w = 1.0f;
		output[3].color = c;
		return;
	}

	const float abs_dx = fabsf(dx);
	const float abs_dy = fabsf(dy);
	float fill_dx, fill_dy;
	float dxdk, dydk;

	float pad_x0 = 0.0f;
	float pad_x1 = 0.0f;
	float pad_y0 = 0.0f;
	float pad_y1 = 0.0f;

	// Check for vertical or horizontal major lines.
	// When expanding to a rect, do so in the appropriate direction.
	// FIXME: This scheme seems to kinda work, but it seems very hard to find a method
	// that looks perfect on every game.
	// Vagrant Story speech bubbles are a very good test case here!
	if (abs_dx > abs_dy)
	{
		fill_dx = 0.0f;
		fill_dy = 1.0f;
		dxdk = 1.0f;
		dydk = dy / abs_dx;

		if (dx > 0.0f)
		{
			// Right
			pad_x1 = 1.0f;
			pad_y1 = dydk;
		}
		else
		{
			// Left
			pad_x0 = 1.0f;
			pad_y0 = -dydk;
		}
	}
	else
	{
		fill_dx = 1.0f;
		fill_dy = 0.0f;
		dydk = 1.0f;
		dxdk = dx / abs_dy;

		if (dy > 0.0f)
		{
			// Down
			pad_y1 = 1.0f;
			pad_x1 = dxdk;
		}
		else
		{
			// Up
			pad_y0 = 1.0f;
			pad_x0 = -dxdk;
		}
	}

	const float x0 = input[0].x + pad_x0;
	const float y0 = input[0].y + pad_y0;
	const float c0 = input[0].color;
	const float x1 = input[1].x + pad_x1;
	const float y1 = input[1].y + pad_y1;
	const float c1 = input[1].color;

	output[0].x = x0;
	output[0].y = y0;
	output[1].x = x0 + fill_dx;
	output[1].y = y0 + fill_dy;
	output[2].x = x1;
	output[2].y = y1;
	output[3].x = x1 + fill_dx;
	output[3].y = y1 + fill_dy;

	output[0].w = 1.0f;
	output[0].color = c0;
	output[1].w = 1.0f;
	output[1].color = c0;
	output[2].w = 1.0f;
	output[2].color = c1;
	output[3].w = 1.0f;
	output[3].color = c1;
}

void Renderer::draw_line(const Vertex *vertices)
{
	// We can move this to GPU, but means more draw calls and more pipeline swapping.
	// This should be plenty fast for the quite small amount of lines games render.
	Vertex vert[4];
	build_line_quad(vert, vertices);
	draw_quad(vert);
}

void Renderer::draw_triangle(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	ih_reset(&last_scanout);

	BufferVertex vert[3];
	HdTextureHandle hd_texture_index = HdTextureHandle::make_none();
	bool filtering = false;
	bool scaled_read = false;
	unsigned shift = 0;
	bool offset_uv = false;
	build_attribs(vert, vertices, 3, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	const int scissor_index = queue.scissor_invariant ? -1 : int(Rect2DVec_size(&queue.scissors) - 1);
	BufferVertexVec *out = select_pipeline(1, scissor_index, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	if (out)
	{
		for (unsigned i = 0; i < 3; i++)
			BufferVertexVec_push(out, &vert[i]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode_None)
	{
		if (filtering)
			filtering = !get_filer_exclude(FilterExcludeOpaqueAndSemiTrans);

		for (unsigned i = 0; i < 3; i++)
			BufferVertexVec_push(&queue.semi_transparent, &vert[i]);
		{
			SemiTransparentState _sts = { scissor_index, hd_texture_index, render_state.semi_transparent,
		                                         render_state.texture_mode != TextureMode_None,
		                                         render_state.mask_test,
		                                         filtering,
		                                         scaled_read,
												 shift,
												 offset_uv };
			SemiTransparentStateVec_push(&queue.semi_transparent_state, &_sts);
		}

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		// render_pass_is_feedback enables self dependency in renderpass which is necessary for barriers between draws.
		render_pass_is_feedback = true;
	}
}

void Renderer::draw_quad(const Vertex *vertices)
{
	if (!render_state.draw_rect.width || !render_state.draw_rect.height)
		return;

	ih_reset(&last_scanout);

	BufferVertex vert[4];
	// build_attribs may flush the queues, thus calling reset_queue and invalidating any pre-existing HdTextureHandle.
	// tracker.no_hd_texture uses a special index (-1) that is the only one valid across such events.
	// If in the future one were tempted to try to cache or reuse the last used HdTextureHandle here, they would have
	// to be very careful not to let it get invalidated by build_attribs; so any such logic should happen within
	// build_attribs itself, and not out here.
	HdTextureHandle hd_texture_index = HdTextureHandle::make_none();
	bool filtering = false;
	bool scaled_read = false;
	unsigned shift = 0;
	bool offset_uv = false;
	build_attribs(vert, vertices, 4, hd_texture_index, filtering, scaled_read, shift, offset_uv);
	const int scissor_index = queue.scissor_invariant ? -1 : int(Rect2DVec_size(&queue.scissors) - 1);
	BufferVertexVec *out = select_pipeline(2, scissor_index, hd_texture_index, filtering, scaled_read, shift, offset_uv);

	if (out)
	{
		BufferVertexVec_push(out, &vert[0]);
		BufferVertexVec_push(out, &vert[1]);
		BufferVertexVec_push(out, &vert[2]);
		BufferVertexVec_push(out, &vert[3]);
		BufferVertexVec_push(out, &vert[2]);
		BufferVertexVec_push(out, &vert[1]);
	}

	if (render_state.mask_test || render_state.semi_transparent != SemiTransparentMode_None)
	{
		if (filtering)
			filtering = !get_filer_exclude(FilterExcludeOpaqueAndSemiTrans);

		const SemiTransparentState state = {
			scissor_index, hd_texture_index, render_state.semi_transparent,
			render_state.texture_mode != TextureMode_None,
			render_state.mask_test,
			filtering,
			scaled_read,
			shift,
			offset_uv };
		BufferVertexVec_push(&queue.semi_transparent, &vert[0]);
		BufferVertexVec_push(&queue.semi_transparent, &vert[1]);
		BufferVertexVec_push(&queue.semi_transparent, &vert[2]);
		BufferVertexVec_push(&queue.semi_transparent, &vert[3]);
		BufferVertexVec_push(&queue.semi_transparent, &vert[2]);
		BufferVertexVec_push(&queue.semi_transparent, &vert[1]);
		SemiTransparentStateVec_push(&queue.semi_transparent_state, &state);
		SemiTransparentStateVec_push(&queue.semi_transparent_state, &state);

		// We've hit the dragon path, we'll need programmable blending for this render pass.
		render_pass_is_feedback = true;
	}
}

void Renderer::clear_quad(const Rect &rect, uint32_t fb_color, bool candidate)
{
	ih_reset(&last_scanout);
	TextureMode old = fbatlas_set_texture_mode(&atlas, TextureMode_None);
	float z = allocate_depth(Domain_Unscaled, rect);
	fbatlas_set_texture_mode(&atlas, old);

	BufferVertex pos0 = { float(rect.x), float(rect.y), z, 1.0f, FBCOLOR_TO_RGBA8(fb_color) };
	BufferVertex pos1 = { float(rect.x) + float(rect.width), float(rect.y), z, 1.0f, FBCOLOR_TO_RGBA8(fb_color) };
	BufferVertex pos2 = { float(rect.x), float(rect.y) + float(rect.height), z, 1.0f, FBCOLOR_TO_RGBA8(fb_color) };
	BufferVertex pos3 = { float(rect.x) + float(rect.width), float(rect.y) + float(rect.height), z, 1.0f, FBCOLOR_TO_RGBA8(fb_color) };
	BufferVertexVec_push(&queue.opaque, &pos0);
	BufferVertexVec_push(&queue.opaque, &pos1);
	BufferVertexVec_push(&queue.opaque, &pos2);
	BufferVertexVec_push(&queue.opaque, &pos3);
	BufferVertexVec_push(&queue.opaque, &pos2);
	BufferVertexVec_push(&queue.opaque, &pos1);
	{
		PrimitiveInfo _pi0 = PrimitiveInfo(PrimitiveInfoVec_size(&queue.opaque_scissor));
		PrimitiveInfoVec_push(&queue.opaque_scissor, &_pi0);
	}
	{
		PrimitiveInfo _pi1 = PrimitiveInfo(PrimitiveInfoVec_size(&queue.opaque_scissor));
		PrimitiveInfoVec_push(&queue.opaque_scissor, &_pi1);
	}

	if (candidate)
		{ ClearCandidate _vpush = { rect, fb_color, z }; ClearCandidateVec_push(&queue.clear_candidates, &_vpush); }
}

const Renderer::ClearCandidate *Renderer::find_clear_candidate(const Rect &rect) const
{
	const ClearCandidate *ret = NULL;
	{
		int _i;
		for (_i = 0; _i < ClearCandidateVec_size((struct ClearCandidateVec *)&queue.clear_candidates); _i++)
		{
			const ClearCandidate &c = *ClearCandidateVec_at((struct ClearCandidateVec *)&queue.clear_candidates, _i);
			if (c.rect == rect)
				ret = &c;
		}
	}
	return ret;
}

void Renderer::flush_render_pass(const Rect &rect)
{
	ensure_command_buffer();

	RenderPassInfo info = {};

	if (msaa > 1)
		info.color_attachments[0] = &image_get_view(ih_get(&scaled_framebuffer_msaa));
	else
		info.color_attachments[0] = iv_get(&scaled_views.front());

	info.clear_depth_stencil = { 1.0f, 0 };
	info.depth_stencil =
		&device->get_transient_attachment(FB_WIDTH * scaling, FB_HEIGHT * scaling,
		                                 device->get_default_depth_format(), 0, msaa, 1);
	info.num_color_attachments = 1;
	info.store_attachments = 1 << 0;
	info.op_flags = RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;

	RenderPassInfo::Subpass subpass;
	info.num_subpasses = 1;
	info.subpasses = &subpass;
	subpass.num_color_attachments = 1;

	const ClearCandidate *clear_candidate = find_clear_candidate(rect);

	subpass.color_attachments[0] = 0;
	if (render_pass_is_feedback)
	{
		subpass.num_input_attachments = 1;
		subpass.input_attachments[0] = 0;
	}

	if (clear_candidate)
	{
		info.clear_depth_stencil.depth = clear_candidate->z;
		fbcolor_to_rgba32f(info.clear_color[0].float32, clear_candidate->color);
		info.clear_attachments = 1 << 0;
	}
	else
	{
		info.load_attachments = 1 << 0;
	}


	info.render_area.offset = { int(rect.x * scaling), int(rect.y * scaling) };
	info.render_area.extent = { rect.width * scaling, rect.height * scaling };

	commandbuffer_begin_render_pass(cbh_get(&cmd), info);
	commandbuffer_set_scissor(cbh_get(&cmd), info.render_area);
	queue.default_scissor = info.render_area;
	commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 2, image_get_view(ih_get(&dither_lut)), StockSampler_NearestWrap);

	render_opaque_primitives();
	render_opaque_texture_primitives();
	render_semi_transparent_opaque_texture_primitives();
	render_semi_transparent_primitives();

	commandbuffer_end_render_pass(cbh_get(&cmd));

	// Render passes are implicitly synchronized.
	commandbuffer_image_barrier(cbh_get(&cmd), msaa > 1 ? *ih_get(&scaled_framebuffer_msaa) : *ih_get(&scaled_framebuffer),
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);

	reset_queue();
}

void Renderer::dispatch_set_scaled_read_texture(bool scaled_read, bool textured)
{
	if (scaled_read)
	{
		if (msaa > 1)
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer_msaa)), StockSampler_NearestClamp);
		else
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]), StockSampler_NearestClamp);
	}
	else
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)), StockSampler_NearestClamp);
	if (textured)
	{
		if (scaled_read)
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.textured_scaled);
		else
			commandbuffer_set_program(cbh_get(&cmd), *pipelines.textured_unscaled);
	}
	else
	{
		commandbuffer_set_program(cbh_get(&cmd), *pipelines.flat);
	}
}

bool Renderer::primitive_info_sort_gt(const PrimitiveInfo &a, const PrimitiveInfo &b)
{
	if (a.offset_uv != b.offset_uv)
		return a.offset_uv > b.offset_uv;
	if (a.shift != b.shift)
		return a.shift > b.shift;
	if (a.scaled_read != b.scaled_read)
		return a.scaled_read > b.scaled_read;
	if (a.filtering != b.filtering)
		return a.filtering > b.filtering;
	if (a.hd_texture_index != b.hd_texture_index)
		return a.hd_texture_index > b.hd_texture_index;
	if (a.scissor_index != b.scissor_index)
		return a.scissor_index > b.scissor_index;
	return a.triangle_index > b.triangle_index;
}

/* qsort comparator: descending order, matching primitive_info_sort_gt. */
int Renderer::primitive_info_qsort_cmp(const void *pa, const void *pb)
{
	const PrimitiveInfo &a = *(const PrimitiveInfo *)(pa);
	const PrimitiveInfo &b = *(const PrimitiveInfo *)(pb);
	if (primitive_info_sort_gt(a, b))
		return -1;
	if (primitive_info_sort_gt(b, a))
		return 1;
	return 0;
}

void Renderer::dispatch(const BufferVertexVec &vertices, PrimitiveInfoVec &scissors, bool textured)
{
	qsort(PrimitiveInfoVec_data(&scissors), PrimitiveInfoVec_size(&scissors), sizeof(PrimitiveInfo), primitive_info_qsort_cmp);

	// Render flat-shaded primitives.
	BufferVertex *vert = (BufferVertex *)(
	    commandbuffer_allocate_vertex_data(cbh_get(&cmd), 0, BufferVertexVec_size((struct BufferVertexVec *)&vertices) * sizeof(BufferVertex), sizeof(BufferVertex)));

	int scissor = (*PrimitiveInfoVec_front(&scissors)).scissor_index;
	HdTextureHandle hd_texture = (*PrimitiveInfoVec_front(&scissors)).hd_texture_index;
	bool filtering = (*PrimitiveInfoVec_front(&scissors)).filtering;
	bool scaled_read = (*PrimitiveInfoVec_front(&scissors)).scaled_read;
	unsigned shift = (*PrimitiveInfoVec_front(&scissors)).shift;
	bool offset_uv = (*PrimitiveInfoVec_front(&scissors)).offset_uv;
	unsigned last_draw = 0;
	unsigned i = 1;
	unsigned size = PrimitiveInfoVec_size(&scissors);

	hd_texture_uniforms(hd_texture);
	commandbuffer_set_scissor(cbh_get(&cmd), scissor < 0 ? queue.default_scissor : *Rect2DVec_at(&queue.scissors, scissor));
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_FilterMode, filtering ? primitive_filter_mode : FilterMode_NearestNeighbor);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Shift, shift);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_OffsetUV, (int)offset_uv);
	dispatch_set_scaled_read_texture(scaled_read, textured);
	memcpy(vert, BufferVertexVec_data((struct BufferVertexVec *)&vertices) + 3 * (*PrimitiveInfoVec_front(&scissors)).triangle_index, 3 * sizeof(BufferVertex));
	vert += 3;

	for (; i < size; i++, vert += 3)
	{
		if ((*PrimitiveInfoVec_at(&scissors, i)).scissor_index != scissor || (*PrimitiveInfoVec_at(&scissors, i)).hd_texture_index != hd_texture ||
			(*PrimitiveInfoVec_at(&scissors, i)).filtering != filtering || (*PrimitiveInfoVec_at(&scissors, i)).scaled_read != scaled_read || (*PrimitiveInfoVec_at(&scissors, i)).shift != shift ||
			(*PrimitiveInfoVec_at(&scissors, i)).offset_uv != offset_uv)
		{
			unsigned to_draw = i - last_draw;
			commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);
			commandbuffer_draw(cbh_get(&cmd), 3 * to_draw, 1, 3 * last_draw, 0);
			last_draw = i;

			if ((*PrimitiveInfoVec_at(&scissors, i)).scissor_index != scissor) {
				scissor = (*PrimitiveInfoVec_at(&scissors, i)).scissor_index;
				commandbuffer_set_scissor(cbh_get(&cmd), scissor < 0 ? queue.default_scissor : *Rect2DVec_at(&queue.scissors, scissor));
			}
			if ((*PrimitiveInfoVec_at(&scissors, i)).hd_texture_index != hd_texture) {
				hd_texture = (*PrimitiveInfoVec_at(&scissors, i)).hd_texture_index;
				hd_texture_uniforms(hd_texture);
			}
			if ((*PrimitiveInfoVec_at(&scissors, i)).filtering != filtering) {
				filtering = (*PrimitiveInfoVec_at(&scissors, i)).filtering;
				commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_FilterMode, filtering ? primitive_filter_mode : FilterMode_NearestNeighbor);
			}
			if ((*PrimitiveInfoVec_at(&scissors, i)).scaled_read != scaled_read) {
				scaled_read = (*PrimitiveInfoVec_at(&scissors, i)).scaled_read;
				dispatch_set_scaled_read_texture(scaled_read, textured);
			}
			if ((*PrimitiveInfoVec_at(&scissors, i)).shift != shift) {
				shift = (*PrimitiveInfoVec_at(&scissors, i)).shift;
				commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Shift, shift);
			}
			if ((*PrimitiveInfoVec_at(&scissors, i)).offset_uv != offset_uv) {
				offset_uv = (*PrimitiveInfoVec_at(&scissors, i)).offset_uv;
				commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_OffsetUV, (int)offset_uv);
			}
		}
		memcpy(vert, BufferVertexVec_data((struct BufferVertexVec *)&vertices) + 3 * (*PrimitiveInfoVec_at(&scissors, i)).triangle_index, 3 * sizeof(BufferVertex));
	}

	unsigned to_draw = size - last_draw;
	commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);
	commandbuffer_draw(cbh_get(&cmd), 3 * to_draw, 1, 3 * last_draw, 0);
}

void Renderer::render_opaque_primitives()
{
	BufferVertexVec *vertices = &queue.opaque;
	PrimitiveInfoVec *scissors = &queue.opaque_scissor;
	if (BufferVertexVec_empty(vertices))
		return;

	commandbuffer_set_opaque_state(cbh_get(&cmd));
	commandbuffer_set_cull_mode(cbh_get(&cmd), VK_CULL_MODE_NONE);
	commandbuffer_set_depth_compare(cbh_get(&cmd), VK_COMPARE_OP_LESS);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	dispatch(*vertices, *scissors);
}

void Renderer::hd_texture_uniforms(HdTextureHandle hd_texture_index) {
	HdTexture hd = texture_tracker_get_hd_texture(&tracker, hd_texture_index);
	commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 4, image_get_view(ih_get(&hd.texture)), StockSampler_TrilinearClamp); // Type of sampler only matters for the fast path
	// ivec4, ivec4
	struct HDPush {
		int32_t vram_rect_x;
		int32_t vram_rect_y;
		int32_t vram_rect_width;
		int32_t vram_rect_height;

		int32_t texel_rect_x;
		int32_t texel_rect_y;
		int32_t texel_rect_width;
		int32_t texel_rect_height;
	};
	HDPush push = {
		hd.vram_rect.x, hd.vram_rect.y, hd.vram_rect.width, hd.vram_rect.height,
		hd.texel_rect.x, hd.texel_rect.y, hd.texel_rect.width, hd.texel_rect.height
	};
	commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
}

void Renderer::render_semi_transparent_primitives()
{
	unsigned prims = SemiTransparentStateVec_size(&queue.semi_transparent_state);
	if (!prims)
		return;

	unsigned last_draw_offset = 0;

	commandbuffer_set_opaque_state(cbh_get(&cmd));
	commandbuffer_set_cull_mode(cbh_get(&cmd), VK_CULL_MODE_NONE);
	commandbuffer_set_depth_compare(cbh_get(&cmd), VK_COMPARE_OP_LESS);
	commandbuffer_set_depth_test(cbh_get(&cmd), true, false);
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	size_t size = BufferVertexVec_size(&queue.semi_transparent) * sizeof(BufferVertex);
	void *verts = commandbuffer_allocate_vertex_data(cbh_get(&cmd), 0, size, sizeof(BufferVertex));
	memcpy(verts, BufferVertexVec_data(&queue.semi_transparent), size);

	SemiTransparentState last_state = *SemiTransparentStateVec_at(&queue.semi_transparent_state, 0);

	semi_transparent_set_state(last_state);

	// These pixels are blended, so we have to render them in-order.
	// Batch up as long as we can.
	for (unsigned i = 1; i < prims; i++)
	{
		// If we need programmable shading, we can't batch as primitives may overlap.
		// We could in theory do some fancy tests here, but probably overkill here.
		if ((last_state.masked && last_state.semi_transparent != SemiTransparentMode_None) ||
		    (last_state != *SemiTransparentStateVec_at(&queue.semi_transparent_state, i)))
		{
			unsigned to_draw = i - last_draw_offset;
			commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);

			{
				VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
				vkCmdPipelineBarrier(commandbuffer_get_command_buffer(cbh_get(&cmd)),
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_DEPENDENCY_BY_REGION_BIT,
					1, &barrier, 0, NULL, 0, NULL);
			}

			commandbuffer_draw(cbh_get(&cmd), to_draw * 3, 1, last_draw_offset * 3, 0);
			if (msaa > 1)
				commandbuffer_set_multisample_state(cbh_get(&cmd), false);
			last_draw_offset = i;

			last_state = *SemiTransparentStateVec_at(&queue.semi_transparent_state, i);
			semi_transparent_set_state(last_state);
		}
	}

	unsigned to_draw = prims - last_draw_offset;
	commandbuffer_set_specialization_constant_mask(cbh_get(&cmd), -1);
	commandbuffer_draw(cbh_get(&cmd), to_draw * 3, 1, last_draw_offset * 3, 0);
	if (msaa > 1)
		commandbuffer_set_multisample_state(cbh_get(&cmd), false);
}

void Renderer::render_semi_transparent_opaque_texture_primitives()
{
	BufferVertexVec *vertices = &queue.semi_transparent_opaque;
	PrimitiveInfoVec *scissors = &queue.semi_transparent_opaque_scissor;
	if (BufferVertexVec_empty(vertices))
		return;

	commandbuffer_set_opaque_state(cbh_get(&cmd));
	commandbuffer_set_cull_mode(cbh_get(&cmd), VK_CULL_MODE_NONE);
	commandbuffer_set_depth_compare(cbh_get(&cmd), VK_COMPARE_OP_LESS);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_SemiTransOpaque);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Scaling, scaling);
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	dispatch(*vertices, *scissors, true);
}

void Renderer::render_opaque_texture_primitives()
{
	BufferVertexVec *vertices = &queue.opaque_textured;
	PrimitiveInfoVec *scissors = &queue.opaque_textured_scissor;
	if (BufferVertexVec_empty(vertices))
		return;

	commandbuffer_set_opaque_state(cbh_get(&cmd));
	commandbuffer_set_cull_mode(cbh_get(&cmd), VK_CULL_MODE_NONE);
	commandbuffer_set_depth_compare(cbh_get(&cmd), VK_COMPARE_OP_LESS);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_Opaque);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Scaling, scaling);
	commandbuffer_set_primitive_topology(cbh_get(&cmd), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0);
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(BufferVertex, color));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 2, 0, VK_FORMAT_R8G8B8A8_UINT, offsetof(BufferVertex, window));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 3, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, pal_x)); // Pad to support AMD
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 4, 0, VK_FORMAT_R16G16B16A16_SINT, offsetof(BufferVertex, u));
	commandbuffer_set_vertex_attrib(cbh_get(&cmd), 5, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(BufferVertex, min_u));

	dispatch(*vertices, *scissors, true);
}

void Renderer::flush_blits()
{
	ensure_command_buffer();
	flush_blit(queue.scaled_blits, *pipelines.blit_vram_scaled, true);
	flush_blit(queue.scaled_masked_blits, *pipelines.blit_vram_scaled_masked, true);
	flush_blit(queue.unscaled_blits, *pipelines.blit_vram_unscaled, false);
	flush_blit(queue.unscaled_masked_blits, *pipelines.blit_vram_unscaled_masked, false);
	BlitInfoVec_clear(&queue.scaled_blits);
	BlitInfoVec_clear(&queue.scaled_masked_blits);
	BlitInfoVec_clear(&queue.unscaled_blits);
	BlitInfoVec_clear(&queue.unscaled_masked_blits);
}

void Renderer::flush_blit(const BlitInfoVec &infos, Program &program, bool scaled)
{
	if (BlitInfoVec_empty(&infos))
		return;

	commandbuffer_set_program(cbh_get(&cmd), program);

	if (scaled)
	{
		if (msaa > 1)
		{
			commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer_msaa)));
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&scaled_framebuffer_msaa)), StockSampler_NearestClamp);
		}
		else
		{
			commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]));
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, *iv_get(&scaled_views[0]), StockSampler_NearestClamp);
		}
	}
	else
	{
		commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)));
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 1, image_get_view(ih_get(&framebuffer)), StockSampler_NearestClamp);
	}

	unsigned size = BlitInfoVec_size(&infos);
	unsigned scale = scaled ? scaling : 1u;
	for (unsigned i = 0; i < size; i += 512)
	{
		unsigned to_blit = min_(size - i, 512u);
		void *ptr = commandbuffer_allocate_constant_data(cbh_get(&cmd), 1, 0, to_blit * sizeof(BlitInfo));
		memcpy(ptr, BlitInfoVec_data((struct BlitInfoVec *)&infos) + i, to_blit * sizeof(BlitInfo));
		commandbuffer_dispatch(cbh_get(&cmd), scale, scale, to_blit);
	}
}

void Renderer::blit_vram(const Rect &dst, const Rect &src)
{
	VK_ASSERT(dst.width == src.width);
	VK_ASSERT(dst.height == src.height);

	// Happens a lot in Square games for some reason.
	if (dst == src)
		return;

	if (dst.width == 0 || dst.height == 0)
		return;

#ifndef NDEBUG
	TT_LOG_VERBOSE(RETRO_LOG_INFO,
		"blit_vram(dst={%i, %i, %i x %i}, src={%i, %i, %i x %i}).\n", dst.x, dst.y, dst.width, dst.height, src.x, src.y, src.width, src.height
	);
#endif
	ih_reset(&last_scanout);
	Domain domain = fbatlas_blit_vram(&atlas, dst, src);

	if (texture_tracking_enabled) {
		texture_tracker_blit(&tracker, dst, src);
	}

	if (dst.intersects(src))
	{
		// The software implementation takes texture cache into account by copying 128 horizontal pixels at a time ...
		// We can do it with compute.
		ensure_command_buffer();

		unsigned factor = domain == Domain_Scaled ? scaling : 1u;

		// Slower path where we do this in a single workgroup which steps through line by line, just like the software version.
		struct Push
		{
			uint32_t src_offset[2];
			uint32_t dst_offset[2];
			uint32_t extent[2];
			int32_t scaling;
		};
		Push push = {
			{ src.x, src.y }, { dst.x, dst.y }, { dst.width, dst.height }, int(factor),
		};
		commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));

		if (domain == Domain_Scaled)
		{
			if (msaa > 1)
			{
				commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer_msaa)));
				commandbuffer_set_program(cbh_get(&cmd), render_state.mask_test ?
						*pipelines.blit_vram_msaa_cached_scaled_masked :
						*pipelines.blit_vram_msaa_cached_scaled);
				commandbuffer_dispatch(cbh_get(&cmd), factor, factor, msaa);
			}
			else
			{
				commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]));
				commandbuffer_set_program(cbh_get(&cmd), render_state.mask_test ? *pipelines.blit_vram_cached_scaled_masked :
														*pipelines.blit_vram_cached_scaled);
				commandbuffer_dispatch(cbh_get(&cmd), factor, factor, 1);
			}
		}
		else
		{
			commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)));
			commandbuffer_set_program(cbh_get(&cmd), render_state.mask_test ? *pipelines.blit_vram_cached_unscaled_masked :
													*pipelines.blit_vram_cached_unscaled);
			commandbuffer_dispatch(cbh_get(&cmd), factor, factor, 1);
		}
		//LOG("Intersecting blit_vram, hitting slow path (src: %u, %u, dst: %u, %u, size: %u, %u)\n", src.x, src.y, dst.x,
		//    dst.y, dst.width, dst.height);
	}
	else
	{
		if (domain == Domain_Scaled)
		{
			BlitInfoVec *q = render_state.mask_test ? &queue.scaled_masked_blits : &queue.scaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					for (unsigned s = 0; s < msaa; s++)
						{
						BlitInfo _bi = {
							{ (x + src.x) * scaling, (y + src.y) * scaling },
							{ (x + dst.x) * scaling, (y + dst.y) * scaling },
							{ min_(BLOCK_WIDTH, width - x) * scaling, min_(BLOCK_HEIGHT, height - y) * scaling },
							render_state.force_mask_bit ? 0x8000u : 0u, s,
						};
						BlitInfoVec_push(q, &_bi);
					}
		}
		else
		{
			BlitInfoVec *q = render_state.mask_test ? &queue.unscaled_masked_blits : &queue.unscaled_blits;
			unsigned width = dst.width;
			unsigned height = dst.height;
			for (unsigned y = 0; y < height; y += BLOCK_HEIGHT)
				for (unsigned x = 0; x < width; x += BLOCK_WIDTH)
					{
					BlitInfo _bi = {
					    { x + src.x, y + src.y },
					    { x + dst.x, y + dst.y },
					    { min_(BLOCK_WIDTH, width - x), min_(BLOCK_HEIGHT, height - y) },
						render_state.force_mask_bit ? 0x8000u : 0u, 0,
					};
					BlitInfoVec_push(q, &_bi);
				}
		}
	}
}

ImageHandle Renderer::upload_texture(LoadedLevels &levels) {
	ImageCreateInfo info;
	ImageInitialData initial[16]; /* Vulkan caps mip levels well under this */
	int i;
	int n = levels.count;
	if (n > 16)
		n = 16;
	info.width = levels.levels[0].width;
	info.height = levels.levels[0].height;
	info.depth = 1;
	info.levels = n;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.type = VK_IMAGE_TYPE_2D;
	info.layers = 1;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.flags = 0;
	info.misc = 0u;
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	for (i = 0; i < n; i++) {
		initial[i].data = levels.levels[i].owned_data;
		initial[i].row_length = 0;
		initial[i].image_height = 0;
	}

	ImageHandle image = device->create_image(info, initial);
	return image;
}
ImageHandle Renderer::create_texture(int width, int height, int levels) {
	ImageCreateInfo info = ImageCreateInfo::immutable_2d_image(width, height, VK_FORMAT_R8G8B8A8_UNORM, false);
	info.levels = levels;
	info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	ImageHandle image = device->create_image(info, NULL);
	return image;
}

BufferHandle Renderer::copy_cpu_to_vram(const Rect &rect)
{
	ih_reset(&last_scanout);
	fbatlas_load_image(&atlas, rect);
	VkDeviceSize size = rect.width * rect.height * sizeof(uint16_t);

	// TODO: Chain allocate this.
	BufferCreateInfo buffer_create_info;
	buffer_create_info.domain = BufferDomain_Host;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	BufferHandle buffer = device->create_buffer(buffer_create_info, NULL);

	BufferViewCreateInfo view_info = {};
	view_info.buffer = bh_get(&buffer);

	struct Push
	{
		Rect rect;
		uint32_t offset;
		uint32_t mask_or;
	};

	ensure_command_buffer();
	commandbuffer_set_program(cbh_get(&cmd), render_state.mask_test ? *pipelines.copy_to_vram_masked : *pipelines.copy_to_vram);
	commandbuffer_set_storage_texture(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)));

	// Vulkan minimum limit, for large buffer views, split up the work.
	if (rect.width * rect.height > device->get_gpu_properties().limits.maxTexelBufferElements)
	{
		for (unsigned y = 0; y < rect.height; y += BLOCK_HEIGHT)
		{
			unsigned y_size = min_(rect.height - y, BLOCK_HEIGHT);
			view_info.offset = y * rect.width * sizeof(uint16_t);
			view_info.range = y_size * rect.width * sizeof(uint16_t);
			view_info.format = VK_FORMAT_R16_UINT;
			BufferViewHandle view = device->create_buffer_view(view_info);

			Rect small_rect = { rect.x, rect.y + y, rect.width, y_size };

			commandbuffer_set_buffer_view(cbh_get(&cmd), 0, 1, *bvh_get(&view));
			Push push = { small_rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
			commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));
			commandbuffer_dispatch(cbh_get(&cmd), (small_rect.width + 7) >> 3, (small_rect.height + 7) >> 3, 1);
			bvh_reset(&view);
		}
	}
	else
	{
		view_info.offset = 0;
		view_info.range = size;
		view_info.format = VK_FORMAT_R16_UINT;
		BufferViewHandle view = device->create_buffer_view(view_info);

		commandbuffer_set_buffer_view(cbh_get(&cmd), 0, 1, *bvh_get(&view));

		Push push = { rect, 0, render_state.force_mask_bit ? 0x8000u : 0u };
		commandbuffer_push_constants(cbh_get(&cmd), &push, 0, sizeof(push));

		// TODO: Batch up work.
		commandbuffer_dispatch(cbh_get(&cmd), (rect.width + 7) >> 3, (rect.height + 7) >> 3, 1);
		bvh_reset(&view);
	}

	return buffer;
}

Renderer::~Renderer()
{
	flush();
	texture_tracker_fini(&tracker); /* embedded TextureTracker: explicit fini (was default dtor) */
	/* Release the ImageHandle members (previously dropped by implicit member
	 * destructors, which a plain struct no longer provides). */
	ih_reset(&scaled_framebuffer);
	ih_reset(&scaled_framebuffer_msaa);
	ih_reset(&bias_framebuffer);
	ih_reset(&framebuffer);
	ih_reset(&framebuffer_ssaa);
	ih_reset(&dither_lut);
	ih_reset(&last_scanout);
	ih_reset(&reuseable_scanout);
	bh_reset(&quad);
	/* Free the per-frame draw-queue arrays (POD_VEC backing storage). */
	BufferVertexVec_free_storage(&queue.opaque);
	PrimitiveInfoVec_free_storage(&queue.opaque_scissor);
	BufferVertexVec_free_storage(&queue.opaque_textured);
	PrimitiveInfoVec_free_storage(&queue.opaque_textured_scissor);
	BufferVertexVec_free_storage(&queue.semi_transparent_opaque);
	PrimitiveInfoVec_free_storage(&queue.semi_transparent_opaque_scissor);
	BufferVertexVec_free_storage(&queue.semi_transparent);
	SemiTransparentStateVec_free_storage(&queue.semi_transparent_state);
	Rect2DVec_free_storage(&queue.scaled_resolves);
	Rect2DVec_free_storage(&queue.unscaled_resolves);
	BlitInfoVec_free_storage(&queue.scaled_blits);
	BlitInfoVec_free_storage(&queue.scaled_masked_blits);
	BlitInfoVec_free_storage(&queue.unscaled_blits);
	BlitInfoVec_free_storage(&queue.unscaled_masked_blits);
	Rect2DVec_free_storage(&queue.scissors);
	ClearCandidateVec_free_storage(&queue.clear_candidates);
}

void Renderer::reset_scissor_queue()
{
	Rect2DVec_clear(&queue.scissors);
	Rect &rect = render_state.draw_rect;
	{ VkRect2D _vpush = { { int(rect.x * scaling), int(rect.y * scaling) }, { rect.width * scaling, rect.height * scaling } }; Rect2DVec_push(&queue.scissors, &_vpush); }
}

void Renderer::reset_queue()
{
	BufferVertexVec_clear(&queue.opaque);
	PrimitiveInfoVec_clear(&queue.opaque_scissor);
	BufferVertexVec_clear(&queue.opaque_textured);
	PrimitiveInfoVec_clear(&queue.opaque_textured_scissor);
	BufferVertexVec_clear(&queue.semi_transparent);
	SemiTransparentStateVec_clear(&queue.semi_transparent_state);
	BufferVertexVec_clear(&queue.semi_transparent_opaque);
	PrimitiveInfoVec_clear(&queue.semi_transparent_opaque_scissor);
	ClearCandidateVec_clear(&queue.clear_candidates);
	primitive_index = 0;
	render_pass_is_feedback = false;

	reset_scissor_queue();

	if (texture_tracking_enabled) {
		texture_tracker_on_queues_reset(&tracker);
	}
}

void Renderer::semi_transparent_set_state(const SemiTransparentState &state)
{
	if (state.scaled_read)
	{
		if (msaa > 1)
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&scaled_framebuffer_msaa)), StockSampler_NearestClamp);
		else
			commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, *iv_get(&scaled_views[0]), StockSampler_NearestClamp);
	}
	else
		commandbuffer_set_texture_view_stock(cbh_get(&cmd), 0, 0, image_get_view(ih_get(&framebuffer)), StockSampler_NearestClamp);
	hd_texture_uniforms(state.hd_texture_index);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_FilterMode, state.filtering ? primitive_filter_mode : FilterMode_NearestNeighbor);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Scaling, scaling);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_Shift, state.shift);
	commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_OffsetUV, (int)state.offset_uv);

	if (state.scissor_index < 0)
		commandbuffer_set_scissor(cbh_get(&cmd), queue.default_scissor);
	else
		commandbuffer_set_scissor(cbh_get(&cmd), *Rect2DVec_at(&queue.scissors, state.scissor_index));

	Program &textured = state.textured ? state.scaled_read ?
		*pipelines.textured_scaled : *pipelines.textured_unscaled : *pipelines.flat;
	Program &textured_masked = state.textured ? state.scaled_read ?
		*pipelines.textured_masked_scaled : *pipelines.textured_masked_unscaled : *pipelines.flat_masked;

	switch (state.semi_transparent)
	{
	case SemiTransparentMode_None:
	{
		// For opaque primitives which are just masked, we can make use of fixed function blending.
		commandbuffer_set_blend_enable(cbh_get(&cmd), true);
		commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_Opaque);
		commandbuffer_set_program(cbh_get(&cmd), textured);
		commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
		commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
		                       VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA);
		break;
	}
	case SemiTransparentMode_Add:
	{
		if (state.masked)
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_BlendMode, BlendMode_BlendAdd);
			commandbuffer_set_program(cbh_get(&cmd), textured_masked);
			commandbuffer_pixel_barrier(cbh_get(&cmd));
			commandbuffer_set_input_attachments(cbh_get(&cmd), 0, 3);
			commandbuffer_set_blend_enable(cbh_get(&cmd), false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				commandbuffer_set_multisample_state(cbh_get(&cmd), false, false, true);
			}
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_SemiTrans);
			commandbuffer_set_program(cbh_get(&cmd), textured);
			commandbuffer_set_blend_enable(cbh_get(&cmd), true);
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode_Average:
	{
		if (state.masked)
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_BlendMode, BlendMode_BlendAvg);
			commandbuffer_set_program(cbh_get(&cmd), textured_masked);
			commandbuffer_set_input_attachments(cbh_get(&cmd), 0, 3);
			commandbuffer_pixel_barrier(cbh_get(&cmd));
			commandbuffer_set_blend_enable(cbh_get(&cmd), false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				commandbuffer_set_multisample_state(cbh_get(&cmd), false, false, true);
			}
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			static const float rgba[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_SemiTrans);
			commandbuffer_set_program(cbh_get(&cmd), textured);
			commandbuffer_set_blend_enable(cbh_get(&cmd), true);
			commandbuffer_set_blend_constants(cbh_get(&cmd), rgba);
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode_Sub:
	{
		if (state.masked)
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_BlendMode, BlendMode_BlendSub);
			commandbuffer_set_program(cbh_get(&cmd), textured_masked);
			commandbuffer_set_input_attachments(cbh_get(&cmd), 0, 3);
			commandbuffer_pixel_barrier(cbh_get(&cmd));
			commandbuffer_set_blend_enable(cbh_get(&cmd), false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				commandbuffer_set_multisample_state(cbh_get(&cmd), false, false, true);
			}
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_SemiTrans);
			commandbuffer_set_program(cbh_get(&cmd), textured);
			commandbuffer_set_blend_enable(cbh_get(&cmd), true);
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	case SemiTransparentMode_AddQuarter:
	{
		if (state.masked)
		{
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_BlendMode, BlendMode_BlendAddQuarter);
			commandbuffer_set_program(cbh_get(&cmd), textured_masked);
			commandbuffer_set_input_attachments(cbh_get(&cmd), 0, 3);
			commandbuffer_pixel_barrier(cbh_get(&cmd));
			commandbuffer_set_blend_enable(cbh_get(&cmd), false);
			if (msaa > 1)
			{
				// Need to blend per-sample.
				commandbuffer_set_multisample_state(cbh_get(&cmd), false, false, true);
			}
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ONE);
		}
		else
		{
			static const float rgba[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
			commandbuffer_set_specialization_constant(cbh_get(&cmd), SpecConstIndex_TransMode, TransMode_SemiTrans);
			commandbuffer_set_program(cbh_get(&cmd), textured);
			commandbuffer_set_blend_enable(cbh_get(&cmd), true);
			commandbuffer_set_blend_constants(cbh_get(&cmd), rgba);
			commandbuffer_set_blend_op(cbh_get(&cmd), VK_BLEND_OP_ADD, VK_BLEND_OP_ADD);
			commandbuffer_set_blend_factors(cbh_get(&cmd), VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE,
			                       VK_BLEND_FACTOR_ZERO);
		}
		break;
	}
	}
}

/* ============================================================
 *
 * Folded content from the parallel-psx/vulkan/ and
 * parallel-psx/atlas/ source files, in dependency order.
 *
 * ============================================================ */


/* === cookie.cpp === */


static void cookie_init(struct Cookie *self, Device *device)
{
	self->cookie = device->allocate_cookie();
}

/* === texture_format.cpp === */


static uint32_t tfl_num_miplevels(uint32_t width, uint32_t height, uint32_t depth)
{
	uint32_t wh = width > height ? width : height;
	uint32_t size = wh > depth ? wh : depth;
	uint32_t levels = 0;
	while (size)
	{
		levels++;
		size >>= 1;
	}
	return levels;
}

static void tfl_format_block_dim(VkFormat format, uint32_t *width, uint32_t *height)
{
#define fmt(x, w, h)     \
    case VK_FORMAT_##x: \
        *width = w; \
        *height = h; \
        break

	switch (format)
	{
	fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8_UNORM_BLOCK, 4, 4);
	fmt(ETC2_R8G8B8_SRGB_BLOCK, 4, 4);
	fmt(EAC_R11_UNORM_BLOCK, 4, 4);
	fmt(EAC_R11_SNORM_BLOCK, 4, 4);
	fmt(EAC_R11G11_UNORM_BLOCK, 4, 4);
	fmt(EAC_R11G11_SNORM_BLOCK, 4, 4);

	fmt(BC1_RGB_UNORM_BLOCK, 4, 4);
	fmt(BC1_RGB_SRGB_BLOCK, 4, 4);
	fmt(BC1_RGBA_UNORM_BLOCK, 4, 4);
	fmt(BC1_RGBA_SRGB_BLOCK, 4, 4);
	fmt(BC2_UNORM_BLOCK, 4, 4);
	fmt(BC2_SRGB_BLOCK, 4, 4);
	fmt(BC3_UNORM_BLOCK, 4, 4);
	fmt(BC3_SRGB_BLOCK, 4, 4);
	fmt(BC4_UNORM_BLOCK, 4, 4);
	fmt(BC4_SNORM_BLOCK, 4, 4);
	fmt(BC5_UNORM_BLOCK, 4, 4);
	fmt(BC5_SNORM_BLOCK, 4, 4);
	fmt(BC6H_UFLOAT_BLOCK, 4, 4);
	fmt(BC6H_SFLOAT_BLOCK, 4, 4);
	fmt(BC7_SRGB_BLOCK, 4, 4);
	fmt(BC7_UNORM_BLOCK, 4, 4);

	fmt(ASTC_4x4_SRGB_BLOCK, 4, 4);
	fmt(ASTC_5x4_SRGB_BLOCK, 5, 4);
	fmt(ASTC_5x5_SRGB_BLOCK, 5, 5);
	fmt(ASTC_6x5_SRGB_BLOCK, 6, 5);
	fmt(ASTC_6x6_SRGB_BLOCK, 6, 6);
	fmt(ASTC_8x5_SRGB_BLOCK, 8, 5);
	fmt(ASTC_8x6_SRGB_BLOCK, 8, 6);
	fmt(ASTC_8x8_SRGB_BLOCK, 8, 8);
	fmt(ASTC_10x5_SRGB_BLOCK, 10, 5);
	fmt(ASTC_10x6_SRGB_BLOCK, 10, 6);
	fmt(ASTC_10x8_SRGB_BLOCK, 10, 8);
	fmt(ASTC_10x10_SRGB_BLOCK, 10, 10);
	fmt(ASTC_12x10_SRGB_BLOCK, 12, 10);
	fmt(ASTC_12x12_SRGB_BLOCK, 12, 12);
	fmt(ASTC_4x4_UNORM_BLOCK, 4, 4);
	fmt(ASTC_5x4_UNORM_BLOCK, 5, 4);
	fmt(ASTC_5x5_UNORM_BLOCK, 5, 5);
	fmt(ASTC_6x5_UNORM_BLOCK, 6, 5);
	fmt(ASTC_6x6_UNORM_BLOCK, 6, 6);
	fmt(ASTC_8x5_UNORM_BLOCK, 8, 5);
	fmt(ASTC_8x6_UNORM_BLOCK, 8, 6);
	fmt(ASTC_8x8_UNORM_BLOCK, 8, 8);
	fmt(ASTC_10x5_UNORM_BLOCK, 10, 5);
	fmt(ASTC_10x6_UNORM_BLOCK, 10, 6);
	fmt(ASTC_10x8_UNORM_BLOCK, 10, 8);
	fmt(ASTC_10x10_UNORM_BLOCK, 10, 10);
	fmt(ASTC_12x10_UNORM_BLOCK, 12, 10);
	fmt(ASTC_12x12_UNORM_BLOCK, 12, 12);

	default:
		*width = 1;
		*height = 1;
		break;
	}

#undef fmt
}

static uint32_t tfl_format_block_size(VkFormat format)
{
#define fmt(x, bpp)     \
    case VK_FORMAT_##x: \
        return bpp
	switch (format)
	{
	fmt(R4G4_UNORM_PACK8, 1);
	fmt(R4G4B4A4_UNORM_PACK16, 2);
	fmt(B4G4R4A4_UNORM_PACK16, 2);
	fmt(R5G6B5_UNORM_PACK16, 2);
	fmt(B5G6R5_UNORM_PACK16, 2);
	fmt(R5G5B5A1_UNORM_PACK16, 2);
	fmt(B5G5R5A1_UNORM_PACK16, 2);
	fmt(A1R5G5B5_UNORM_PACK16, 2);
	fmt(R8_UNORM, 1);
	fmt(R8_SNORM, 1);
	fmt(R8_USCALED, 1);
	fmt(R8_SSCALED, 1);
	fmt(R8_UINT, 1);
	fmt(R8_SINT, 1);
	fmt(R8_SRGB, 1);
	fmt(R8G8_UNORM, 2);
	fmt(R8G8_SNORM, 2);
	fmt(R8G8_USCALED, 2);
	fmt(R8G8_SSCALED, 2);
	fmt(R8G8_UINT, 2);
	fmt(R8G8_SINT, 2);
	fmt(R8G8_SRGB, 2);
	fmt(R8G8B8_UNORM, 3);
	fmt(R8G8B8_SNORM, 3);
	fmt(R8G8B8_USCALED, 3);
	fmt(R8G8B8_SSCALED, 3);
	fmt(R8G8B8_UINT, 3);
	fmt(R8G8B8_SINT, 3);
	fmt(R8G8B8_SRGB, 3);
	fmt(R8G8B8A8_UNORM, 4);
	fmt(R8G8B8A8_SNORM, 4);
	fmt(R8G8B8A8_USCALED, 4);
	fmt(R8G8B8A8_SSCALED, 4);
	fmt(R8G8B8A8_UINT, 4);
	fmt(R8G8B8A8_SINT, 4);
	fmt(R8G8B8A8_SRGB, 4);
	fmt(B8G8R8A8_UNORM, 4);
	fmt(B8G8R8A8_SNORM, 4);
	fmt(B8G8R8A8_USCALED, 4);
	fmt(B8G8R8A8_SSCALED, 4);
	fmt(B8G8R8A8_UINT, 4);
	fmt(B8G8R8A8_SINT, 4);
	fmt(B8G8R8A8_SRGB, 4);
	fmt(A8B8G8R8_UNORM_PACK32, 4);
	fmt(A8B8G8R8_SNORM_PACK32, 4);
	fmt(A8B8G8R8_USCALED_PACK32, 4);
	fmt(A8B8G8R8_SSCALED_PACK32, 4);
	fmt(A8B8G8R8_UINT_PACK32, 4);
	fmt(A8B8G8R8_SINT_PACK32, 4);
	fmt(A8B8G8R8_SRGB_PACK32, 4);
	fmt(A2B10G10R10_UNORM_PACK32, 4);
	fmt(A2B10G10R10_SNORM_PACK32, 4);
	fmt(A2B10G10R10_USCALED_PACK32, 4);
	fmt(A2B10G10R10_SSCALED_PACK32, 4);
	fmt(A2B10G10R10_UINT_PACK32, 4);
	fmt(A2B10G10R10_SINT_PACK32, 4);
	fmt(A2R10G10B10_UNORM_PACK32, 4);
	fmt(A2R10G10B10_SNORM_PACK32, 4);
	fmt(A2R10G10B10_USCALED_PACK32, 4);
	fmt(A2R10G10B10_SSCALED_PACK32, 4);
	fmt(A2R10G10B10_UINT_PACK32, 4);
	fmt(A2R10G10B10_SINT_PACK32, 4);
	fmt(R16_UNORM, 2);
	fmt(R16_SNORM, 2);
	fmt(R16_USCALED, 2);
	fmt(R16_SSCALED, 2);
	fmt(R16_UINT, 2);
	fmt(R16_SINT, 2);
	fmt(R16_SFLOAT, 2);
	fmt(R16G16_UNORM, 4);
	fmt(R16G16_SNORM, 4);
	fmt(R16G16_USCALED, 4);
	fmt(R16G16_SSCALED, 4);
	fmt(R16G16_UINT, 4);
	fmt(R16G16_SINT, 4);
	fmt(R16G16_SFLOAT, 4);
	fmt(R16G16B16_UNORM, 6);
	fmt(R16G16B16_SNORM, 6);
	fmt(R16G16B16_USCALED, 6);
	fmt(R16G16B16_SSCALED, 6);
	fmt(R16G16B16_UINT, 6);
	fmt(R16G16B16_SINT, 6);
	fmt(R16G16B16_SFLOAT, 6);
	fmt(R16G16B16A16_UNORM, 8);
	fmt(R16G16B16A16_SNORM, 8);
	fmt(R16G16B16A16_USCALED, 8);
	fmt(R16G16B16A16_SSCALED, 8);
	fmt(R16G16B16A16_UINT, 8);
	fmt(R16G16B16A16_SINT, 8);
	fmt(R16G16B16A16_SFLOAT, 8);
	fmt(R32_UINT, 4);
	fmt(R32_SINT, 4);
	fmt(R32_SFLOAT, 4);
	fmt(R32G32_UINT, 8);
	fmt(R32G32_SINT, 8);
	fmt(R32G32_SFLOAT, 8);
	fmt(R32G32B32_UINT, 12);
	fmt(R32G32B32_SINT, 12);
	fmt(R32G32B32_SFLOAT, 12);
	fmt(R32G32B32A32_UINT, 16);
	fmt(R32G32B32A32_SINT, 16);
	fmt(R32G32B32A32_SFLOAT, 16);
	fmt(R64_UINT, 8);
	fmt(R64_SINT, 8);
	fmt(R64_SFLOAT, 8);
	fmt(R64G64_UINT, 16);
	fmt(R64G64_SINT, 16);
	fmt(R64G64_SFLOAT, 16);
	fmt(R64G64B64_UINT, 24);
	fmt(R64G64B64_SINT, 24);
	fmt(R64G64B64_SFLOAT, 24);
	fmt(R64G64B64A64_UINT, 32);
	fmt(R64G64B64A64_SINT, 32);
	fmt(R64G64B64A64_SFLOAT, 32);
	fmt(B10G11R11_UFLOAT_PACK32, 4);
	fmt(E5B9G9R9_UFLOAT_PACK32, 4);
	fmt(D16_UNORM, 2);
	fmt(X8_D24_UNORM_PACK32, 4);
	fmt(D32_SFLOAT, 4);
	fmt(S8_UINT, 1);
	fmt(D16_UNORM_S8_UINT, 3); // Doesn't make sense.
	fmt(D24_UNORM_S8_UINT, 4);
	fmt(D32_SFLOAT_S8_UINT, 5); // Doesn't make sense.

		// ETC2
	fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 16);
	fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 16);
	fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 8);
	fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 8);
	fmt(ETC2_R8G8B8_UNORM_BLOCK, 8);
	fmt(ETC2_R8G8B8_SRGB_BLOCK, 8);
	fmt(EAC_R11_UNORM_BLOCK, 8);
	fmt(EAC_R11_SNORM_BLOCK, 8);
	fmt(EAC_R11G11_UNORM_BLOCK, 16);
	fmt(EAC_R11G11_SNORM_BLOCK, 16);

		// BC
	fmt(BC1_RGB_UNORM_BLOCK, 8);
	fmt(BC1_RGB_SRGB_BLOCK, 8);
	fmt(BC1_RGBA_UNORM_BLOCK, 8);
	fmt(BC1_RGBA_SRGB_BLOCK, 8);
	fmt(BC2_UNORM_BLOCK, 16);
	fmt(BC2_SRGB_BLOCK, 16);
	fmt(BC3_UNORM_BLOCK, 16);
	fmt(BC3_SRGB_BLOCK, 16);
	fmt(BC4_UNORM_BLOCK, 8);
	fmt(BC4_SNORM_BLOCK, 8);
	fmt(BC5_UNORM_BLOCK, 16);
	fmt(BC5_SNORM_BLOCK, 16);
	fmt(BC6H_UFLOAT_BLOCK, 16);
	fmt(BC6H_SFLOAT_BLOCK, 16);
	fmt(BC7_SRGB_BLOCK, 16);
	fmt(BC7_UNORM_BLOCK, 16);

		// ASTC
	fmt(ASTC_4x4_SRGB_BLOCK, 16);
	fmt(ASTC_5x4_SRGB_BLOCK, 16);
	fmt(ASTC_5x5_SRGB_BLOCK, 16);
	fmt(ASTC_6x5_SRGB_BLOCK, 16);
	fmt(ASTC_6x6_SRGB_BLOCK, 16);
	fmt(ASTC_8x5_SRGB_BLOCK, 16);
	fmt(ASTC_8x6_SRGB_BLOCK, 16);
	fmt(ASTC_8x8_SRGB_BLOCK, 16);
	fmt(ASTC_10x5_SRGB_BLOCK, 16);
	fmt(ASTC_10x6_SRGB_BLOCK, 16);
	fmt(ASTC_10x8_SRGB_BLOCK, 16);
	fmt(ASTC_10x10_SRGB_BLOCK, 16);
	fmt(ASTC_12x10_SRGB_BLOCK, 16);
	fmt(ASTC_12x12_SRGB_BLOCK, 16);
	fmt(ASTC_4x4_UNORM_BLOCK, 16);
	fmt(ASTC_5x4_UNORM_BLOCK, 16);
	fmt(ASTC_5x5_UNORM_BLOCK, 16);
	fmt(ASTC_6x5_UNORM_BLOCK, 16);
	fmt(ASTC_6x6_UNORM_BLOCK, 16);
	fmt(ASTC_8x5_UNORM_BLOCK, 16);
	fmt(ASTC_8x6_UNORM_BLOCK, 16);
	fmt(ASTC_8x8_UNORM_BLOCK, 16);
	fmt(ASTC_10x5_UNORM_BLOCK, 16);
	fmt(ASTC_10x6_UNORM_BLOCK, 16);
	fmt(ASTC_10x8_UNORM_BLOCK, 16);
	fmt(ASTC_10x10_UNORM_BLOCK, 16);
	fmt(ASTC_12x10_UNORM_BLOCK, 16);
	fmt(ASTC_12x12_UNORM_BLOCK, 16);

	default:
		assert(0 && "Unknown format.");
		return 0;
	}
#undef fmt
}

static void tfl_fill_mipinfo(struct TextureFormatLayout *self, uint32_t width, uint32_t height, uint32_t depth)
{
	uint32_t mip;
	size_t offset = 0;
	self->block_stride = tfl_format_block_size(self->format);
	tfl_format_block_dim(self->format, &self->block_dim_x, &self->block_dim_y);

	if (self->mip_levels == 0)
		self->mip_levels = tfl_num_miplevels(width, height, depth);

	for (mip = 0; mip < self->mip_levels; mip++)
	{
		uint32_t blocks_x, blocks_y, next_w, next_h, next_d;
		size_t mip_size;
		offset = (offset + 15) & ~(size_t)15;

		blocks_x = (width + self->block_dim_x - 1) / self->block_dim_x;
		blocks_y = (height + self->block_dim_y - 1) / self->block_dim_y;
		mip_size = blocks_x * blocks_y * self->array_layers * depth * self->block_stride;

		self->mips[mip].offset = offset;

		self->mips[mip].block_row_length = blocks_x;
		self->mips[mip].block_image_height = blocks_y;

		self->mips[mip].row_length = blocks_x * self->block_dim_x;
		self->mips[mip].image_height = blocks_y * self->block_dim_y;

		self->mips[mip].width = width;
		self->mips[mip].height = height;
		self->mips[mip].depth = depth;

		offset += mip_size;

		next_w = width >> 1u;
		next_h = height >> 1u;
		next_d = depth >> 1u;
		width = next_w > 1u ? next_w : 1u;
		height = next_h > 1u ? next_h : 1u;
		depth = next_d > 1u ? next_d : 1u;
	}

	self->required_size = offset;
}

static void tfl_set_1d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t array_layers, uint32_t mip_levels)
{
	self->image_type = VK_IMAGE_TYPE_1D;
	self->format = format;
	self->array_layers = array_layers;
	self->mip_levels = mip_levels;

	tfl_fill_mipinfo(self, width, 1, 1);
}

static void tfl_set_2d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t height, uint32_t array_layers, uint32_t mip_levels)
{
	self->image_type = VK_IMAGE_TYPE_2D;
	self->format = format;
	self->array_layers = array_layers;
	self->mip_levels = mip_levels;

	tfl_fill_mipinfo(self, width, height, 1);
}

static void tfl_set_3d(struct TextureFormatLayout *self, VkFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mip_levels)
{
	self->image_type = VK_IMAGE_TYPE_3D;
	self->format = format;
	self->array_layers = 1;
	self->mip_levels = mip_levels;

	tfl_fill_mipinfo(self, width, height, depth);
}

static void tfl_build_buffer_image_copies(const struct TextureFormatLayout *self, VkBufferImageCopy *copies, unsigned *num_copies)
{
	unsigned level;
	assert(self->mip_levels <= 16);
	*num_copies = self->mip_levels;
	for (level = 0; level < self->mip_levels; level++)
	{
		const TextureFormatLayoutMipInfo *mip_info = &self->mips[level];

		VkBufferImageCopy *blit = &copies[level];
		memset(blit, 0, sizeof(*blit));
		blit->bufferOffset = mip_info->offset;
		blit->bufferRowLength = mip_info->row_length;
		blit->bufferImageHeight = mip_info->image_height;
		blit->imageSubresource.aspectMask = format_to_aspect_mask(self->format);
		blit->imageSubresource.mipLevel = level;
		blit->imageSubresource.baseArrayLayer = 0;
		blit->imageSubresource.layerCount = self->array_layers;
		blit->imageExtent.width = mip_info->width;
		blit->imageExtent.height = mip_info->height;
		blit->imageExtent.depth = mip_info->depth;
	}
}


/* === sampler.cpp === */


static void sampler_init(struct Sampler *self, Device *device, VkSampler sampler)
{
	self->device  = device;
	self->sampler = sampler;
	counter_init(&self->reference_count); /* refcount starts at 1 */
	cookie_init(&self->cookie_base, device);
}

void sampler_fini(struct Sampler *self)
{
	if (self->sampler)
		self->device->destroy_sampler_nolock(self->sampler);
}

static void sampler_release_reference(struct Sampler *self)
{
	if (counter_release(&self->reference_count))
		SamplerDeleter()(self);
}

void SamplerDeleter::operator()(Sampler *sampler)
{
	{ struct ObjectPoolRaw *_pool = &sampler->device->handle_pool.samplers; sampler_fini(sampler); object_pool_raw_free(_pool, sampler); }
}

/* === buffer.cpp === */


static void buffer_init(struct Buffer *self, Device *device, VkBuffer buffer, const DeviceAllocation &alloc, const BufferCreateInfo &info)
{
	self->device = device;
	self->buffer = buffer;
	self->alloc  = alloc;
	self->info   = info;
	counter_init(&self->reference_count); /* refcount starts at 1 */
	cookie_init(&self->cookie_base, device);
}

void buffer_fini(struct Buffer *self)
{
	self->device->destroy_buffer_nolock(self->buffer);
	self->device->free_memory_nolock(self->alloc);
}

static void buffer_release_reference(struct Buffer *self)
{
	if (counter_release(&self->reference_count))
		BufferDeleter()(self);
}

void BufferDeleter::operator()(Buffer *buffer)
{
	{ struct ObjectPoolRaw *_pool = &buffer->device->handle_pool.buffers; buffer_fini(buffer); object_pool_raw_free(_pool, buffer); }
}

static void bufferview_init(struct BufferView *self, Device *device, VkBufferView view, const BufferViewCreateInfo &create_info)
{
	self->device = device;
	self->view   = view;
	self->info   = create_info;
	counter_init(&self->reference_count); /* refcount starts at 1 */
	cookie_init(&self->cookie_base, device);
}

void bufferview_fini(struct BufferView *self)
{
	if (self->view != VK_NULL_HANDLE)
		self->device->destroy_buffer_view_nolock(self->view);
}

static void bufferview_release_reference(struct BufferView *self)
{
	if (counter_release(&self->reference_count))
		BufferViewDeleter()(self);
}

void BufferViewDeleter::operator()(BufferView *view)
{
	{ struct ObjectPoolRaw *_pool = &view->device->handle_pool.buffer_views; bufferview_fini(view); object_pool_raw_free(_pool, view); }
}


/* === image.cpp === */



static void imageview_init(struct ImageView *self, Device *device, VkImageView view, const ImageViewCreateInfo &info)
{
	self->device       = device;
	self->view         = view;
	/* Members that previously had default initializers in the class body. */
	self->render_target_views.items = NULL;
	self->render_target_views.count = 0;
	self->render_target_views.cap   = 0;
	self->depth_view   = VK_NULL_HANDLE;
	self->stencil_view = VK_NULL_HANDLE;
	self->info         = info;
	counter_init(&self->reference_count); /* refcount starts at 1 */
	cookie_init(&self->cookie_base, device);
}

static VkImageView imageview_get_render_target_view(const struct ImageView *self, unsigned layer)
{
	// Transient images just have one layer.
	if (image_get_create_info(self->info.image).domain == ImageDomain_Transient)
		return self->view;

	VK_ASSERT(layer < imageview_get_create_info(self).layers);

	if (RenderTargetViewVec_empty(&self->render_target_views))
		return self->view;
	else
	{
		VK_ASSERT(layer < (unsigned)RenderTargetViewVec_size(&self->render_target_views));
		return *RenderTargetViewVec_at((struct RenderTargetViewVec *)&self->render_target_views, layer);
	}
}

void imageview_fini(struct ImageView *self)
{
	self->device->destroy_image_view_nolock(self->view);
	if (self->depth_view != VK_NULL_HANDLE)
		self->device->destroy_image_view_nolock(self->depth_view);
	if (self->stencil_view != VK_NULL_HANDLE)
		self->device->destroy_image_view_nolock(self->stencil_view);

	{
		int _i;
		for (_i = 0; _i < RenderTargetViewVec_size(&self->render_target_views); _i++)
			self->device->destroy_image_view_nolock(*RenderTargetViewVec_at(&self->render_target_views, _i));
	}
	RenderTargetViewVec_free_storage(&self->render_target_views);
}

static void imageview_release_reference(struct ImageView *self)
{
	if (counter_release(&self->reference_count))
		ImageViewDeleter()(self);
}

void image_init(struct Image *self, Device *device, VkImage image, VkImageView default_view, const DeviceAllocation &alloc,
             const ImageCreateInfo &create_info)
{
	self->device      = device;
	self->image       = image;
	self->alloc       = alloc;
	self->create_info = create_info;
	/* Members that previously had default initializers in the class body. */
	self->layout_type  = Layout_Optimal;
	self->stage_flags  = 0;
	self->access_flags = 0;
	counter_init(&self->reference_count); /* refcount starts at 1 */
	cookie_init(&self->cookie_base, device);
	/* ImageViewHandle is a plain struct with no default member initializer;
	 * explicitly null the view so image_fini's iv_reset is safe on the path
	 * where no default view is created. */
	self->view.data = NULL;
	if (default_view != VK_NULL_HANDLE)
	{
		ImageViewCreateInfo info;
		info.image = self;
		info.format = create_info.format;
		info.base_level = 0;
		info.levels = create_info.levels;
		info.base_layer = 0;
		info.layers = create_info.layers;
		{ struct ImageView *_iv = (struct ImageView *)object_pool_raw_allocate(&device->handle_pool.image_views); imageview_init(_iv, device, default_view, info); self->view = iv_make(_iv); }
	}
}

void image_fini(struct Image *self)
{
	/* The default-view handle was previously released by the implicit
	 * ImageViewHandle member destructor; now that it is a plain struct, drop
	 * its reference explicitly. */
	iv_reset(&self->view);
	if (deviceallocation_get_memory(&self->alloc))
	{
		self->device->destroy_image_nolock(self->image);
		self->device->free_memory_nolock(self->alloc);
	}
}

static void image_release_reference(struct Image *self)
{
	if (counter_release(&self->reference_count))
		ImageDeleter()(self);
}

void ImageViewDeleter::operator()(ImageView *view)
{
	{ struct ObjectPoolRaw *_pool = &view->device->handle_pool.image_views; imageview_fini(view); object_pool_raw_free(_pool, view); }
}

void ImageDeleter::operator()(Image *image)
{
	{ struct ObjectPoolRaw *_pool = &image->device->handle_pool.images; image_fini(image); object_pool_raw_free(_pool, image); }
}

/* === fence.cpp === */


static void fenceholder_init(struct FenceHolder *self, Device *device, VkFence fence)
{
	self->device = device;
	self->fence  = fence;
	counter_init(&self->reference_count); /* refcount starts at 1 */
}

void fenceholder_fini(struct FenceHolder *self)
{
	if (self->fence != VK_NULL_HANDLE)
		self->device->reset_fence(self->fence);
}

static void fenceholder_wait(struct FenceHolder *self)
{
	/* A null fence means no work was ever submitted against it (e.g. a failed
	 * vkQueueSubmit handed back VK_NULL_HANDLE); there is nothing to wait for and
	 * waiting on a null handle is invalid, so treat it as already signalled. */
	if (self->fence == VK_NULL_HANDLE)
		return;
	if (vkWaitForFences(self->device->get_device(), 1, &self->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
		LOGE("Failed to wait for fence!\n");
}

static void fenceholder_release_reference(struct FenceHolder *self)
{
	if (counter_release(&self->reference_count))
		FenceHolderDeleter()(self);
}

void FenceHolderDeleter::operator()(FenceHolder *fence)
{
	{ struct ObjectPoolRaw *_pool = &fence->device->handle_pool.fences; fenceholder_fini(fence); object_pool_raw_free(_pool, fence); }
}

/* === fence_manager.cpp === */


static VkFence fencemanager_request_cleared_fence(struct FenceManager *self)
{
	if (!FenceVec_empty(&self->fences))
	{
		VkFence ret = *FenceVec_back(&self->fences);
		FenceVec_pop_back(&self->fences);
		return ret;
	}
	else
	{
		VkFence fence;
		VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		vkCreateFence(self->device, &info, NULL, &fence);
		return fence;
	}
}

static void fencemanager_recycle_fence(struct FenceManager *self, VkFence fence)
{
	FenceVec_push(&self->fences, &fence);
}

static void fencemanager_deinit(struct FenceManager *self)
{
	int _i;
	for (_i = 0; _i < FenceVec_size(&self->fences); _i++)
		vkDestroyFence(self->device, *FenceVec_at(&self->fences, _i), NULL);
	FenceVec_free_storage(&self->fences);
}

/* === semaphore.cpp === */


static void semaphoreholder_init(struct SemaphoreHolder *self, Device *device, VkSemaphore semaphore, bool signalled)
{
	self->device    = device;
	self->semaphore = semaphore;
	self->signalled = signalled;
	counter_init(&self->reference_count); /* refcount starts at 1 */
}

void semaphoreholder_fini(struct SemaphoreHolder *self)
{
	if (self->semaphore)
	{
		if (semaphoreholder_is_signalled(self))
			self->device->destroy_semaphore_nolock(self->semaphore);
		else
			self->device->recycle_semaphore_nolock(self->semaphore);
	}
}

static void semaphoreholder_release_reference(struct SemaphoreHolder *self)
{
	if (counter_release(&self->reference_count))
		SemaphoreHolderDeleter()(self);
}

void SemaphoreHolderDeleter::operator()(SemaphoreHolder *semaphore)
{
	{ struct ObjectPoolRaw *_pool = &semaphore->device->handle_pool.semaphores; semaphoreholder_fini(semaphore); object_pool_raw_free(_pool, semaphore); }
}

/* === semaphore_manager.cpp === */


static void semaphoremanager_deinit(struct SemaphoreManager *self)
{
	int _i;
	for (_i = 0; _i < SemaphoreVec_size(&self->semaphores); _i++)
		vkDestroySemaphore(self->device, *SemaphoreVec_at(&self->semaphores, _i), NULL);
	SemaphoreVec_free_storage(&self->semaphores);
}

static void semaphoremanager_recycle(struct SemaphoreManager *self, VkSemaphore sem)
{
	if (sem != VK_NULL_HANDLE)
		SemaphoreVec_push(&self->semaphores, &sem);
}

static VkSemaphore semaphoremanager_request_cleared_semaphore(struct SemaphoreManager *self)
{
	if (SemaphoreVec_empty(&self->semaphores))
	{
		VkSemaphore semaphore;
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		vkCreateSemaphore(self->device, &info, NULL, &semaphore);
		return semaphore;
	}
	else
	{
		VkSemaphore sem = *SemaphoreVec_back(&self->semaphores);
		SemaphoreVec_pop_back(&self->semaphores);
		return sem;
	}
}

/* === buffer_pool.cpp === */


static void bufferpool_reset(struct BufferPool *self)
{
	bufferblock_vec_clear(&self->blocks);
}

static struct BufferBlock bufferpool_allocate_block(struct BufferPool *self, VkDeviceSize size)
{
	struct BufferBlock block;
	BufferCreateInfo info;
	bufferblock_init(&block);

	info.domain = BufferDomain_Host;
	info.size = size;
	info.usage = self->usage;

	block.gpu = self->device->create_buffer(info, NULL);
	self->device->set_name(*bh_get(&block.gpu), "chain-allocated-block-gpu");

	// Try to map it, will fail unless the memory is host visible.
	block.mapped = (uint8_t *)(self->device->map_host_buffer(*bh_get(&block.gpu), MEMORY_ACCESS_WRITE_BIT));
	if (!block.mapped)
	{
		// Fall back to host memory, and remember to sync to gpu on submission time using DMA queue. :)
		BufferCreateInfo cpu_info;
		cpu_info.domain = BufferDomain_Host;
		cpu_info.size = size;
		cpu_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		block.cpu = self->device->create_buffer(cpu_info, NULL);
		self->device->set_name(*bh_get(&block.cpu), "chain-allocated-block-cpu");
		block.mapped = (uint8_t *)(self->device->map_host_buffer(*bh_get(&block.cpu), MEMORY_ACCESS_WRITE_BIT));
	}
	else
		bh_assign(&block.cpu, &block.gpu);

	block.offset = 0;
	block.alignment = self->alignment;
	block.size = size;
	return block;
}

static struct BufferBlock bufferpool_request_block(struct BufferPool *self, VkDeviceSize minimum_size)
{
	if ((minimum_size > self->block_size) || bufferblock_vec_empty(&self->blocks))
	{
		VkDeviceSize alloc_size = self->block_size > minimum_size ? self->block_size : minimum_size;
		return bufferpool_allocate_block(self, alloc_size);
	}
	else
	{
		struct BufferBlock back;
		/* steal the last recycled block (no refcount change), then drop the now-
		 * emptied slot from the vector (pop_back finis a null-handle block, a
		 * no-op). */
		bufferblock_steal(&back, bufferblock_vec_back(&self->blocks));
		bufferblock_vec_pop_back(&self->blocks);

		back.mapped = (uint8_t *)(self->device->map_host_buffer(*bh_get(&back.cpu), MEMORY_ACCESS_WRITE_BIT));
		back.offset = 0;
		return back;
	}
}

static void bufferpool_recycle_block(struct BufferPool *self, struct BufferBlock *block)
{
	VK_ASSERT(block->size == self->block_size);
	/* copy-retain into the recycle list; the caller still owns *block and must
	 * bufferblock_fini it (matching the old BufferBlock&& whose moved-from temp
	 * was destroyed at the call site). */
	bufferblock_vec_push(&self->blocks, block);
}


/* === command_pool.cpp === */


static void command_pool_init(CommandPool *cp, VkDevice device, uint32_t queue_family_index)
{
	cp->device = device;
	cp->pool = VK_NULL_HANDLE;
	cp->buffers.items = NULL; cp->buffers.count = 0; cp->buffers.cap = 0;
#ifdef VULKAN_DEBUG
	cp->in_flight.items = NULL; cp->in_flight.count = 0; cp->in_flight.cap = 0;
#endif
	cp->index = 0;

	VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	info.queueFamilyIndex = queue_family_index;
	vkCreateCommandPool(device, &info, NULL, &cp->pool);
}

static void command_pool_deinit(CommandPool *cp)
{
	if (!CommandBufferVec_empty(&cp->buffers))
		vkFreeCommandBuffers(cp->device, cp->pool, CommandBufferVec_size(&cp->buffers), CommandBufferVec_data(&cp->buffers));
	if (cp->pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(cp->device, cp->pool, NULL);
	CommandBufferVec_free_storage(&cp->buffers);
#ifdef VULKAN_DEBUG
	CommandBufferVec_free_storage(&cp->in_flight);
#endif
}

VkCommandBuffer command_pool_request_command_buffer(CommandPool *self)
{
	if (self->index < CommandBufferVec_size(&self->buffers))
	{
		VkCommandBuffer ret = *CommandBufferVec_at(&self->buffers, self->index++);
#ifdef VULKAN_DEBUG
		{
			bool present = false;
			int i;
			for (i = 0; i < self->in_flight.count; i++)
				if (self->in_flight.items[i] == ret) { present = true; break; }
			VK_ASSERT(!present);
			CommandBufferVec_push(&self->in_flight, &ret);
		}
#endif
		return ret;
	}
	else
	{
		VkCommandBuffer cmd;
		VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		info.commandPool = self->pool;
		info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		info.commandBufferCount = 1;

		vkAllocateCommandBuffers(self->device, &info, &cmd);
#ifdef VULKAN_DEBUG
		{
			bool present = false;
			int i;
			for (i = 0; i < self->in_flight.count; i++)
				if (self->in_flight.items[i] == cmd) { present = true; break; }
			VK_ASSERT(!present);
			CommandBufferVec_push(&self->in_flight, &cmd);
		}
#endif
		CommandBufferVec_push(&self->buffers, &cmd);
		self->index++;
		return cmd;
	}
}

void command_pool_begin(CommandPool *self)
{
#ifdef VULKAN_DEBUG
	VK_ASSERT(CommandBufferVec_empty(&self->in_flight));
#endif
	if (self->index > 0)
		vkResetCommandPool(self->device, self->pool, 0);
	self->index = 0;
}

/* === memory_allocator.cpp === */


void deviceallocation_free_immediate(struct DeviceAllocation *self)
{
	if (!self->alloc)
		return;

	classallocator_free(self->alloc, self);
	self->alloc = NULL;
	self->base = VK_NULL_HANDLE;
	self->mask = 0;
	self->offset = 0;
}

void deviceallocation_free_immediate_alloc(struct DeviceAllocation *self, DeviceAllocator *allocator)
{
	if (self->alloc)
		deviceallocation_free_immediate(self);
	else if (self->base)
	{
		deviceallocator_free_no_recycle(allocator, self->size, self->memory_type, self->base, self->host_base);
		self->base = VK_NULL_HANDLE;
	}
}

void deviceallocation_free_global(struct DeviceAllocation *self, DeviceAllocator *allocator, uint32_t size, uint32_t memory_type)
{
	if (self->base)
	{
		deviceallocator_free(allocator, size, memory_type, self->base, self->host_base);
		self->base = VK_NULL_HANDLE;
		self->mask = 0;
		self->offset = 0;
	}
}

void block_allocate(struct Block *self, uint32_t num_blocks, DeviceAllocation *block)
{
	VK_ASSERT(BLOCK_NUM_SUB_BLOCKS >= num_blocks);
	VK_ASSERT(num_blocks != 0);

	uint32_t block_mask;
	if (num_blocks == BLOCK_NUM_SUB_BLOCKS)
		block_mask = ~0u;
	else
		block_mask = ((1u << num_blocks) - 1u);

	uint32_t mask = self->free_blocks[num_blocks - 1];
	uint32_t b = trailing_zeroes(mask);

	VK_ASSERT(((self->free_blocks[0] >> b) & block_mask) == block_mask);

	uint32_t sb = block_mask << b;
	self->free_blocks[0] &= ~sb;
	block_update_longest_run(self);

	block->mask = sb;
	block->offset = b;
}

void block_free(struct Block *self, uint32_t mask)
{
	VK_ASSERT((self->free_blocks[0] & mask) == 0);
	self->free_blocks[0] |= mask;
	block_update_longest_run(self);
}

void classallocator_suballocate(struct ClassAllocator *self, uint32_t num_blocks, uint32_t tiling, uint32_t memory_type, struct MiniHeap *heap,
                                 struct DeviceAllocation *alloc)
{
	block_allocate(&heap->heap, num_blocks, alloc);
	alloc->base = heap->allocation.base;
	alloc->offset <<= self->sub_block_size_log2;

	if (heap->allocation.host_base)
		alloc->host_base = heap->allocation.host_base + alloc->offset;

	alloc->offset += heap->allocation.offset;
	alloc->tiling = tiling;
	alloc->memory_type = memory_type;
	alloc->alloc = self;
	alloc->size = num_blocks << self->sub_block_size_log2;
}

bool classallocator_allocate(struct ClassAllocator *self, uint32_t size, AllocationTiling tiling, struct DeviceAllocation *alloc, bool hierarchical)
{
	unsigned num_blocks = (size + self->sub_block_size - 1) >> self->sub_block_size_log2;
	uint32_t size_mask = (1u << (num_blocks - 1)) - 1;
	uint32_t masked_tiling_mode = self->tiling_mask & tiling;
	struct ClassAllocatorTilingHeaps *m = &self->tiling_modes[masked_tiling_mode];

	uint32_t index = trailing_zeroes(m->heap_availability_mask & ~size_mask);

	if (index < BLOCK_NUM_SUB_BLOCKS)
	{
		MiniHeap *itr = (MiniHeap *)ilist_begin(&m->heaps[index]);
		VK_ASSERT(itr);
		VK_ASSERT(index >= (num_blocks - 1));

		MiniHeap &heap = *itr;
		classallocator_suballocate(self, num_blocks, masked_tiling_mode, self->memory_type, &heap, alloc);
		unsigned new_index = block_get_longest_run(&heap.heap) - 1;

		if (block_full(&heap.heap))
		{
			ilist_move_to_front(&m->full_heaps, &m->heaps[index], &itr->list_node);
			if (!ilist_begin(&m->heaps[index]))
				m->heap_availability_mask &= ~(1u << index);
		}
		else if (new_index != index)
		{
			struct IntrusiveListC &new_heap = m->heaps[new_index];
			ilist_move_to_front(&new_heap, &m->heaps[index], &itr->list_node);
			m->heap_availability_mask |= 1u << new_index;
			if (!ilist_begin(&m->heaps[index]))
				m->heap_availability_mask &= ~(1u << index);
		}

		alloc->heap = itr;
		alloc->hierarchical = hierarchical;

		return true;
	}

	// We didn't find a vacant heap, make a new one.
	MiniHeap *node = new (object_pool_raw_allocate(&self->object_pool)) MiniHeap();
	if (!node)
		return false;
	/* Block is a plain struct now; its former default ctor (fill the free
	 * bitmap) runs explicitly via block_init. */
	block_init(&node->heap);

	MiniHeap &heap = *node;
	uint32_t alloc_size = self->sub_block_size * BLOCK_NUM_SUB_BLOCKS;

	if (self->parent)
	{
		// We cannot allocate a new block from parent ... This is fatal.
		if (!classallocator_allocate(self->parent, alloc_size, tiling, &heap.allocation, true))
		{
			block_fini(&node->heap); node->~MiniHeap(); object_pool_raw_free(&self->object_pool, node);
			return false;
		}
	}
	else
	{
		heap.allocation.offset = 0;
		if (!deviceallocator_allocate(self->global_allocator, alloc_size, self->memory_type, &heap.allocation.base, &heap.allocation.host_base,
		                                VK_NULL_HANDLE))
		{
			block_fini(&node->heap); node->~MiniHeap(); object_pool_raw_free(&self->object_pool, node);
			return false;
		}
	}

	// This cannot fail.
	classallocator_suballocate(self, num_blocks, masked_tiling_mode, self->memory_type, &heap, alloc);

	alloc->heap = node;
	if (block_full(&heap.heap))
	{
		ilist_insert_front(&m->full_heaps, &node->list_node);
	}
	else
	{
		unsigned new_index = block_get_longest_run(&heap.heap) - 1;
		ilist_insert_front(&m->heaps[new_index], &node->list_node);
		m->heap_availability_mask |= 1u << new_index;
	}

	alloc->hierarchical = hierarchical;

	return true;
}

void classallocator_init(struct ClassAllocator *self)
{
	unsigned t, i;
	self->parent = NULL;
	for (t = 0; t < ALLOCATION_TILING_COUNT; t++)
	{
		for (i = 0; i < BLOCK_NUM_SUB_BLOCKS; i++)
			ilist_clear(&self->tiling_modes[t].heaps[i]);
		ilist_clear(&self->tiling_modes[t].full_heaps);
		self->tiling_modes[t].heap_availability_mask = 0;
	}
	self->sub_block_size = 1;
	self->sub_block_size_log2 = 0;
	self->tiling_mask = ~0u;
	self->memory_type = 0;
	self->global_allocator = NULL;
	object_pool_raw_init(&self->object_pool, sizeof(struct MiniHeap));
}

void classallocator_fini(struct ClassAllocator *self)
{
	bool error = false;
	unsigned t, i;
	for (t = 0; t < ALLOCATION_TILING_COUNT; t++)
	{
		struct ClassAllocatorTilingHeaps *m = &self->tiling_modes[t];
		if (ilist_begin(&m->full_heaps))
			error = true;

		for (i = 0; i < BLOCK_NUM_SUB_BLOCKS; i++)
			if (ilist_begin(&m->heaps[i]))
				error = true;
	}

	if (error)
		LOGE("Memory leaked in class allocator!\n");

	object_pool_raw_deinit(&self->object_pool);
}

void classallocator_free(struct ClassAllocator *self, struct DeviceAllocation *alloc)
{
	struct MiniHeap *heap = alloc->heap;
	struct Block *block = &heap->heap;
	bool was_full = block_full(block);
	struct ClassAllocatorTilingHeaps *m = &self->tiling_modes[alloc->tiling];

	unsigned index = block_get_longest_run(block) - 1;
	block_free(block, alloc->mask);
	unsigned new_index = block_get_longest_run(block) - 1;

	if (block_empty(block))
	{
		// Our mini-heap is completely freed, free to higher level allocator.
		if (self->parent)
			deviceallocation_free_immediate(&heap->allocation);
		else
			deviceallocation_free_global(&heap->allocation, self->global_allocator, self->sub_block_size * BLOCK_NUM_SUB_BLOCKS, self->memory_type);

		if (was_full)
			ilist_erase(&m->full_heaps, &heap->list_node);
		else
		{
			ilist_erase(&m->heaps[index], &heap->list_node);
			if (!ilist_begin(&m->heaps[index]))
				m->heap_availability_mask &= ~(1u << index);
		}

		block_fini(&heap->heap); heap->~MiniHeap(); object_pool_raw_free(&self->object_pool, heap);
	}
	else if (was_full)
	{
		ilist_move_to_front(&m->heaps[new_index], &m->full_heaps, &heap->list_node);
		m->heap_availability_mask |= 1u << new_index;
	}
	else if (index != new_index)
	{
		ilist_move_to_front(&m->heaps[new_index], &m->heaps[index], &heap->list_node);
		m->heap_availability_mask |= 1u << new_index;
		if (!ilist_begin(&m->heaps[index]))
			m->heap_availability_mask &= ~(1u << index);
	}
}

bool alloc_allocate_global(struct Allocator *self, uint32_t size, struct DeviceAllocation *alloc)
{
	// Fall back to global allocation, do not recycle.
	if (!deviceallocator_allocate(self->global_allocator, size, self->memory_type, &alloc->base, &alloc->host_base, VK_NULL_HANDLE))
		return false;
	alloc->alloc = NULL;
	alloc->memory_type = self->memory_type;
	alloc->size = size;
	return true;
}

bool alloc_allocate_dedicated(struct Allocator *self, uint32_t size, struct DeviceAllocation *alloc, VkImage dedicated_image)
{
	// Fall back to global allocation, do not recycle.
	if (!deviceallocator_allocate(self->global_allocator, size, self->memory_type, &alloc->base, &alloc->host_base, dedicated_image))
		return false;
	alloc->alloc = NULL;
	alloc->memory_type = self->memory_type;
	alloc->size = size;
	return true;
}

bool alloc_allocate(struct Allocator *self, uint32_t size, uint32_t alignment, AllocationTiling mode, struct DeviceAllocation *alloc)
{
	unsigned ci;
	for (ci = 0; ci < MEMORY_CLASS_COUNT; ci++)
	{
		struct ClassAllocator *c = &self->classes[ci];
		// Find a suitable class to allocate from.
		if (size <= c->sub_block_size * BLOCK_NUM_SUB_BLOCKS)
		{
			if (alignment > c->sub_block_size)
			{
				size_t padded_size = size + (alignment - c->sub_block_size);
				if (padded_size <= c->sub_block_size * BLOCK_NUM_SUB_BLOCKS)
					size = padded_size;
				else
					continue;
			}

			bool ret = classallocator_allocate(c, size, mode, alloc, false);
			if (ret)
			{
				uint32_t aligned_offset = (alloc->offset + alignment - 1) & ~(alignment - 1);
				if (alloc->host_base)
					alloc->host_base += aligned_offset - alloc->offset;
				alloc->offset = aligned_offset;
			}
			return ret;
		}
	}

	return alloc_allocate_global(self, size, alloc);
}

void alloc_init(struct Allocator *self)
{
	unsigned i;
	for (i = 0; i < MEMORY_CLASS_COUNT; i++)
		classallocator_init(&self->classes[i]);
	self->global_allocator = NULL;
	self->memory_type = 0;

	for (i = 0; i < MEMORY_CLASS_COUNT - 1; i++)
		classallocator_set_parent(&self->classes[i], &self->classes[i + 1]);

	classallocator_set_tiling_mask(alloc_get_class_allocator(self, MEMORY_CLASS_SMALL), ~0u);
	classallocator_set_tiling_mask(alloc_get_class_allocator(self, MEMORY_CLASS_MEDIUM), ~0u);
	classallocator_set_tiling_mask(alloc_get_class_allocator(self, MEMORY_CLASS_LARGE), 0);
	classallocator_set_tiling_mask(alloc_get_class_allocator(self, MEMORY_CLASS_HUGE), 0);

	classallocator_set_sub_block_size(alloc_get_class_allocator(self, MEMORY_CLASS_SMALL), 128);
	classallocator_set_sub_block_size(alloc_get_class_allocator(self, MEMORY_CLASS_MEDIUM), 128 * BLOCK_NUM_SUB_BLOCKS); // 4K

	// 128K, this is the largest bufferImageGranularity a Vulkan implementation may have.
	classallocator_set_sub_block_size(alloc_get_class_allocator(self, MEMORY_CLASS_LARGE), 128 * BLOCK_NUM_SUB_BLOCKS * BLOCK_NUM_SUB_BLOCKS);
	classallocator_set_sub_block_size(alloc_get_class_allocator(self, MEMORY_CLASS_HUGE),
	    64 * BLOCK_NUM_SUB_BLOCKS * BLOCK_NUM_SUB_BLOCKS * BLOCK_NUM_SUB_BLOCKS); // 2M
}

void deviceallocator_init(struct DeviceAllocator *self, VkPhysicalDevice gpu, VkDevice vkdevice)
{
	unsigned i;
	VkPhysicalDeviceProperties props;
	self->device = vkdevice;
	vkGetPhysicalDeviceMemoryProperties(gpu, &self->mem_props);

	vkGetPhysicalDeviceProperties(gpu, &props);
	self->atom_alignment = props.limits.nonCoherentAtomSize;

	da_heap_vec_clear(&self->heaps);
	AllocatorPtrVec_clear(&self->allocators);

	da_heap_vec_resize(&self->heaps, (int)self->mem_props.memoryHeapCount);
	for (i = 0; i < self->mem_props.memoryTypeCount; i++)
	{
		Allocator *a = (Allocator *)malloc(sizeof(Allocator));
		alloc_init(a);
		AllocatorPtrVec_push(&self->allocators, a);
		alloc_set_memory_type(AllocatorPtrVec_back(&self->allocators), i);
		alloc_set_global_allocator(AllocatorPtrVec_back(&self->allocators), self);
	}
}

bool deviceallocator_allocate_image_memory(struct DeviceAllocator *self, uint32_t size, uint32_t alignment, uint32_t memory_type,
                                            AllocationTiling tiling, struct DeviceAllocation *alloc, VkImage image)
{
	if (!self->use_dedicated)
		return deviceallocator_allocate_typed(self, size, alignment, memory_type, tiling, alloc);

	VkImageMemoryRequirementsInfo2KHR info = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2_KHR };
	info.image = image;

	VkMemoryDedicatedRequirementsKHR dedicated_req = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2KHR mem_req = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR };
	mem_req.pNext = &dedicated_req;
	vkGetImageMemoryRequirements2KHR(self->device, &info, &mem_req);

	if (dedicated_req.prefersDedicatedAllocation || dedicated_req.requiresDedicatedAllocation)
		return alloc_allocate_dedicated(self->allocators.items[memory_type], size, alloc, image);
	else
		return deviceallocator_allocate_typed(self, size, alignment, memory_type, tiling, alloc);
}

void da_heap_garbage_collect(struct DeviceAllocatorHeap *self, VkDevice device)
{
	int _i;
	for (_i = 0; _i < da_alloc_vec_size(&self->blocks); _i++)
	{
		struct DeviceAllocatorAllocation *block = da_alloc_vec_at(&self->blocks, _i);
		if (block->host_memory)
			vkUnmapMemory(device, block->memory);
		vkFreeMemory(device, block->memory, NULL);
		self->size -= block->size;
	}
}

void deviceallocator_free(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	struct DeviceAllocatorHeap *heap = da_heap_vec_at(&self->heaps, self->mem_props.memoryTypes[memory_type].heapIndex);
	{ struct DeviceAllocatorAllocation a = { memory, host_memory, size, memory_type }; da_alloc_vec_push(&heap->blocks, &a); }
}

void deviceallocator_free_no_recycle(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory)
{
	struct DeviceAllocatorHeap *heap = da_heap_vec_at(&self->heaps, self->mem_props.memoryTypes[memory_type].heapIndex);
	if (host_memory)
		vkUnmapMemory(self->device, memory);
	vkFreeMemory(self->device, memory, NULL);
	heap->size -= size;
}

void deviceallocator_garbage_collect(struct DeviceAllocator *self)
{
	int i;
	for (i = 0; i < da_heap_vec_size(&self->heaps); i++)
		da_heap_garbage_collect(da_heap_vec_at(&self->heaps, i), self->device);
}

void *deviceallocator_map_memory(struct DeviceAllocator *self, const struct DeviceAllocation *alloc, MemoryAccessFlags flags)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc->host_base)
		return NULL;

	if ((flags & MEMORY_ACCESS_READ_BIT) &&
	    !(self->mem_props.memoryTypes[alloc->memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = alloc->offset & ~(self->atom_alignment - 1);
		VkDeviceSize size = (alloc->offset + deviceallocation_get_size(alloc) - offset + self->atom_alignment - 1) & ~(self->atom_alignment - 1);

		// Have to invalidate cache here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, alloc->base, offset, size,
		};
		vkInvalidateMappedMemoryRanges(self->device, 1, &range);
	}

	return alloc->host_base;
}

void deviceallocator_unmap_memory(struct DeviceAllocator *self, const struct DeviceAllocation *alloc, MemoryAccessFlags flags)
{
	// This will only happen if the memory type is device local only, which we cannot possibly map.
	if (!alloc->host_base)
		return;

	if ((flags & MEMORY_ACCESS_WRITE_BIT) &&
	    !(self->mem_props.memoryTypes[alloc->memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		VkDeviceSize offset = alloc->offset & ~(self->atom_alignment - 1);
		VkDeviceSize size = (alloc->offset + deviceallocation_get_size(alloc) - offset + self->atom_alignment - 1) & ~(self->atom_alignment - 1);

		// Have to flush caches here.
		const VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, alloc->base, offset, size,
		};
		vkFlushMappedMemoryRanges(self->device, 1, &range);
	}
}

bool deviceallocator_allocate(struct DeviceAllocator *self, uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory,
                               VkImage dedicated_image)
{
	struct DeviceAllocatorHeap *heap = da_heap_vec_at(&self->heaps, self->mem_props.memoryTypes[memory_type].heapIndex);

	// Naive searching is fine here as vkAllocate blocks are *huge* and we won't have many of them.
	size_t found_idx = (size_t)da_alloc_vec_size(&heap->blocks);
	for (size_t i = 0; i < (size_t)da_alloc_vec_size(&heap->blocks); i++)
	{
		if (da_alloc_vec_at(&heap->blocks, (int)i)->size == size && da_alloc_vec_at(&heap->blocks, (int)i)->type == memory_type)
		{
			found_idx = i;
			break;
		}
	}

	bool host_visible = (self->mem_props.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

	// Found previously used block.
	if (found_idx < (size_t)da_alloc_vec_size(&heap->blocks))
	{
		struct DeviceAllocatorAllocation *block = da_alloc_vec_at(&heap->blocks, (int)found_idx);
		*memory = block->memory;
		*host_memory = block->host_memory;
		da_alloc_vec_erase_at(&heap->blocks, (int)found_idx);
		return true;
	}

	VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, size, memory_type };
	VkMemoryDedicatedAllocateInfoKHR dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
	if (dedicated_image != VK_NULL_HANDLE)
	{
		dedicated.image = dedicated_image;
		info.pNext = &dedicated;
	}

	VkDeviceMemory device_memory;
	VkResult res = vkAllocateMemory(self->device, &info, NULL, &device_memory);

	if (res == VK_SUCCESS)
	{
		heap->size += size;
		*memory = device_memory;

		if (host_visible)
		{
			if (vkMapMemory(self->device, device_memory, 0, size, 0, (void **)(host_memory)) != VK_SUCCESS)
				return false;
		}

		return true;
	}
	else
	{
		// Look through our heap and see if there are blocks of other types we can free.
		int freed = 0;
		while (res != VK_SUCCESS && freed < (int)da_alloc_vec_size(&heap->blocks))
		{
			struct DeviceAllocatorAllocation *b = da_alloc_vec_at(&heap->blocks, (int)freed);
			if (b->host_memory)
				vkUnmapMemory(self->device, b->memory);
			vkFreeMemory(self->device, b->memory, NULL);
			heap->size -= b->size;
			res = vkAllocateMemory(self->device, &info, NULL, &device_memory);
			++freed;
		}

		da_alloc_vec_erase_front(&heap->blocks, (int)freed);

		if (res == VK_SUCCESS)
		{
			heap->size += size;
			*memory = device_memory;

			if (host_visible)
			{
				if (vkMapMemory(self->device, device_memory, 0, size, 0, (void **)(host_memory)) !=
				    VK_SUCCESS)
				{
					vkFreeMemory(self->device, device_memory, NULL);
					return false;
				}
			}

			return true;
		}
		else
			return false;
	}
}

/* === shader.cpp === */

#include "rhi_spirv_reflect.h"

#ifdef GRANITE_SPIRV_DUMP
#include "filesystem.hpp"
#endif


	void pipeline_layout_init(struct PipelineLayout *self, Hash hash, Device *device, const CombinedResourceLayout &layout)
	{
		unsigned i;
		VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		unsigned num_sets = 0;
		self->device = device;
		self->layout = layout;
		self->pipe_layout = VK_NULL_HANDLE;
		self->intrusive_node.key = hash;
		for (i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
		{
			self->set_allocators[i] = device->request_descriptor_set_allocator(layout.sets[i], layout.stages_for_bindings[i]);
			layouts[i] = descriptor_set_allocator_get_layout(self->set_allocators[i]);
			if (layout.descriptor_set_mask & (1u << i))
				num_sets = i + 1;
		}

		VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		if (num_sets)
		{
			info.setLayoutCount = num_sets;
			info.pSetLayouts = layouts;
		}

		if (layout.push_constant_range.stageFlags != 0)
		{
			info.pushConstantRangeCount = 1;
			info.pPushConstantRanges = &layout.push_constant_range;
		}

		LOGI("Creating pipeline layout.\n");
		if (vkCreatePipelineLayout(device->get_device(), &info, NULL, &self->pipe_layout) != VK_SUCCESS)
			LOGE("Failed to create pipeline layout.\n");
	}

	void pipeline_layout_fini(struct PipelineLayout *self)
	{
		if (self->pipe_layout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(self->device->get_device(), self->pipe_layout, NULL);
	}

	const char *shader_stage_to_name(ShaderStage stage)
	{
		switch (stage)
		{
			case ShaderStage_Compute:
				return "compute";
			case ShaderStage_Vertex:
				return "vertex";
			case ShaderStage_Fragment:
				return "fragment";
			case ShaderStage_Geometry:
				return "geometry";
			case ShaderStage_TessControl:
				return "tess_control";
			case ShaderStage_TessEvaluation:
				return "tess_evaluation";
			default:
				return "unknown";
		}
	}

	void shader_init(struct Shader *self, Hash hash, Device *device, const uint32_t *data, size_t size)
	{
		self->device = device;
		self->module = VK_NULL_HANDLE;
		self->intrusive_node.key = hash;
#ifdef GRANITE_SPIRV_DUMP
		{
			char spirv_dump_path[64];
			FILE *spirv_dump_f;
			snprintf(spirv_dump_path, sizeof(spirv_dump_path),
					"cache_spirv_%016llx.spv", (unsigned long long)hash);
			spirv_dump_f = fopen(spirv_dump_path, "wb");
			if (!spirv_dump_f || fwrite(data, 1, size, spirv_dump_f) != size)
				LOGE("Failed to dump shader to file.\n");
			if (spirv_dump_f)
				fclose(spirv_dump_f);
		}
#endif

		VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		info.codeSize = size;
		info.pCode = data;

		LOGI("Creating shader module.\n");
		if (vkCreateShaderModule(device->get_device(), &info, NULL, &self->module) != VK_SUCCESS)
			LOGE("Failed to create shader module.\n");

		/* SPIR-V reflection is done in a separate C++ shim (rhi_spirv_reflect.cpp)
		 * so this translation unit does not depend on SPIRV-Cross. The shim writes
		 * a POD layout whose fields mirror ResourceLayout; copy it across. */
		RhiSpirvResourceLayout reflected;
		rhi_spirv_reflect(data, size / sizeof(uint32_t), &reflected);
		static_assert(sizeof(reflected) == sizeof(self->layout),
				"reflection layout mirror size mismatch");
		memcpy(&self->layout, &reflected, sizeof(self->layout));
	}

	void shader_fini(struct Shader *self)
	{
		if (self->module)
			vkDestroyShaderModule(self->device->get_device(), self->module, NULL);
	}

	void program_init_graphics(struct Program *self, Device *device, Shader *vertex, Shader *fragment)
	{
		unsigned i;
		self->device = device;
		self->layout = NULL;
		for (i = 0; i < (unsigned)ShaderStage_Count; i++)
			self->shaders[i] = NULL;
		vk_pipeline_map_init(&self->pipelines);
		program_set_shader(self, ShaderStage_Vertex, vertex);
		program_set_shader(self, ShaderStage_Fragment, fragment);
		device->bake_program(*self);
	}

	void program_init_compute(struct Program *self, Device *device, Shader *compute)
	{
		unsigned i;
		self->device = device;
		self->layout = NULL;
		for (i = 0; i < (unsigned)ShaderStage_Count; i++)
			self->shaders[i] = NULL;
		vk_pipeline_map_init(&self->pipelines);
		program_set_shader(self, ShaderStage_Compute, compute);
		device->bake_program(*self);
	}

	VkPipeline program_get_pipeline(const struct Program *self, Hash hash)
	{
		IntrusivePODWrapperPipeline *ret = vk_pipeline_map_find((struct vk_pipeline_map *)&self->pipelines, hash);
		return ret ? ret->value : VK_NULL_HANDLE;
	}

	VkPipeline program_add_pipeline(struct Program *self, Hash hash, VkPipeline pipeline)
	{
		return vk_pipeline_map_emplace_yield(&self->pipelines, hash, pipeline)->value;
	}

	void program_fini(struct Program *self)
	{
		struct IntrusiveListNode *n;
		for (n = vk_pipeline_map_begin(&self->pipelines); n; n = n->next)
			self->device->destroy_pipeline_nolock(vk_pipeline_map_iter_get(n)->value);
	}

/* === descriptor_set.cpp === */


	void descriptor_set_allocator_init(struct DescriptorSetAllocator *self, Hash hash, Device *device, const DescriptorSetLayout &layout, const uint32_t *stages_for_binds)
	{
		self->device = device;
		self->set_layout = VK_NULL_HANDLE;
		self->per_thread.pools.items = NULL; self->per_thread.pools.count = 0; self->per_thread.pools.cap = 0;
		self->per_thread.should_begin = true;
		self->pool_size.items = NULL; self->pool_size.count = 0; self->pool_size.cap = 0;
		self->intrusive_node.key = hash;
		/* The set_nodes temp-map was previously brought up by the TemporaryHashmap
		 * default constructor; as a concrete POD member it must be initialised here. */
		descriptor_set_thmap_init_empty(&self->per_thread.set_nodes);
		VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

		DescriptorBindingVec bindings = { NULL, 0, 0 };
		/* Immutable-sampler handles referenced by pImmutableSamplers must stay alive
		 * until vkCreateDescriptorSetLayout below; keep one slot per binding index in
		 * function scope rather than taking the address of a loop-body local (which
		 * would dangle by the time the create call reads it). */
		VkSampler immutable_samplers[VULKAN_NUM_BINDINGS];
		for (unsigned i = 0; i < VULKAN_NUM_BINDINGS; i++)
		{
			uint32_t stages = stages_for_binds[i];
			if (stages == 0)
				continue;

			unsigned types = 0;
			if (layout.sampled_image_mask & (1u << i))
			{
				immutable_samplers[i] = VK_NULL_HANDLE;
				if (has_immutable_sampler(layout, i))
					immutable_samplers[i] = sampler_get_sampler(&device->get_stock_sampler(get_immutable_sampler(layout, i)));

				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, immutable_samplers[i] != VK_NULL_HANDLE ? &immutable_samplers[i] : NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.sampled_buffer_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.storage_image_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.uniform_buffer_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.storage_buffer_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.input_attachment_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.separate_image_mask & (1u << i))
			{
				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			if (layout.sampler_mask & (1u << i))
			{
				immutable_samplers[i] = VK_NULL_HANDLE;
				if (has_immutable_sampler(layout, i))
					immutable_samplers[i] = sampler_get_sampler(&device->get_stock_sampler(get_immutable_sampler(layout, i)));

				{ VkDescriptorSetLayoutBinding _vpush = { i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, immutable_samplers[i] != VK_NULL_HANDLE ? &immutable_samplers[i] : NULL }; DescriptorBindingVec_push(&bindings, &_vpush); }
				{ VkDescriptorPoolSize _vpush = { VK_DESCRIPTOR_TYPE_SAMPLER, VULKAN_NUM_SETS_PER_POOL }; DescriptorPoolSizeVec_push(&self->pool_size, &_vpush); }
				types++;
			}

			(void)types;
			VK_ASSERT(types <= 1 && "Descriptor set aliasing!");
		}

		if (!DescriptorBindingVec_empty(&bindings))
		{
			info.bindingCount = DescriptorBindingVec_size(&bindings);
			info.pBindings = DescriptorBindingVec_data(&bindings);
		}

		LOGI("Creating descriptor set layout.\n");
		if (vkCreateDescriptorSetLayout(self->device->get_device(), &info, NULL, &self->set_layout) != VK_SUCCESS)
			LOGE("Failed to create descriptor set layout.");
		DescriptorBindingVec_free_storage(&bindings);
	}

	DescriptorSetAllocation descriptor_set_allocator_find(struct DescriptorSetAllocator *self, Hash hash)
	{
		struct DescriptorSetAllocatorPerThread *state = &self->per_thread;
		DescriptorSetNode *node;
		if (state->should_begin)
		{
			descriptor_set_thmap_begin_frame(&state->set_nodes);
			state->should_begin = false;
		}

		node = descriptor_set_thmap_request(&state->set_nodes, hash);
		if (node)
		{
			DescriptorSetAllocation r; r.set = node->set; r.cached = true; return r;
		}

		node = descriptor_set_thmap_request_vacant(&state->set_nodes, hash);
		if (node)
		{
			DescriptorSetAllocation r; r.set = node->set; r.cached = false; return r;
		}

		VkDescriptorPool pool;
		VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.maxSets = VULKAN_NUM_SETS_PER_POOL;
		if (!DescriptorPoolSizeVec_empty(&self->pool_size))
		{
			info.poolSizeCount = DescriptorPoolSizeVec_size(&self->pool_size);
			info.pPoolSizes = DescriptorPoolSizeVec_data(&self->pool_size);
		}

		if (vkCreateDescriptorPool(self->device->get_device(), &info, NULL, &pool) != VK_SUCCESS)
			LOGE("Failed to create descriptor pool.\n");

		VkDescriptorSet sets[VULKAN_NUM_SETS_PER_POOL];
		VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
		for (unsigned i = 0; i < VULKAN_NUM_SETS_PER_POOL; i++)
			layouts[i] = self->set_layout;

		VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		alloc.descriptorPool = pool;
		alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
		alloc.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(self->device->get_device(), &alloc, sets) != VK_SUCCESS)
			LOGE("Failed to allocate descriptor sets.\n");
		DescriptorPoolVec_push(&state->pools, &pool);

		{
			unsigned _si;
			for (_si = 0; _si < VULKAN_NUM_SETS_PER_POOL; _si++)
				descriptor_set_thmap_make_vacant(&state->set_nodes, sets[_si]);
		}

		{
			DescriptorSetAllocation r;
			r.set = descriptor_set_thmap_request_vacant(&state->set_nodes, hash)->set;
			r.cached = false;
			return r;
		}
	}

	void descriptor_set_allocator_clear(struct DescriptorSetAllocator *self)
	{
		int _i;
		descriptor_set_thmap_clear(&self->per_thread.set_nodes);
		for (_i = 0; _i < DescriptorPoolVec_size(&self->per_thread.pools); _i++)
		{
			VkDescriptorPool pool = *DescriptorPoolVec_at(&self->per_thread.pools, _i);
			vkResetDescriptorPool(self->device->get_device(), pool, 0);
			vkDestroyDescriptorPool(self->device->get_device(), pool, NULL);
		}
		DescriptorPoolVec_clear(&self->per_thread.pools);
	}

	void descriptor_set_allocator_fini(struct DescriptorSetAllocator *self)
	{
		if (self->set_layout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(self->device->get_device(), self->set_layout, NULL);
		descriptor_set_allocator_clear(self);
		descriptor_set_thmap_deinit(&self->per_thread.set_nodes);
		DescriptorPoolVec_free_storage(&self->per_thread.pools);
		DescriptorPoolSizeVec_free_storage(&self->pool_size);
	}

/* === render_pass.cpp === */


	/* Concrete stack allocators (de-templated StackAllocator<T, N>). Two
	 * instantiations were used - VkAttachmentReference x1024 and uint32_t x1024 -
	 * each a fixed buffer plus a bump offset. allocate returns NULL when the count
	 * is zero or would overflow; allocate_cleared additionally zero-fills the
	 * returned slots. offset must be zeroed at declaration (the template's default
	 * member initialiser). */
	struct StackAllocatorRef
	{
		VkAttachmentReference buffer[1024];
		size_t offset;
	};

	static inline VkAttachmentReference *stackalloc_ref_allocate(struct StackAllocatorRef *a, size_t count)
	{
		VkAttachmentReference *ret;
		if (count == 0)
			return NULL;
		if (a->offset + count > 1024)
			return NULL;
		ret = a->buffer + a->offset;
		a->offset += count;
		return ret;
	}

	static inline VkAttachmentReference *stackalloc_ref_allocate_cleared(struct StackAllocatorRef *a, size_t count)
	{
		VkAttachmentReference *ret = stackalloc_ref_allocate(a, count);
		if (ret)
		{
			size_t i;
			for (i = 0; i < count; i++)
				memset(&ret[i], 0, sizeof(ret[i]));
		}
		return ret;
	}

	struct StackAllocatorU32
	{
		uint32_t buffer[1024];
		size_t offset;
	};

	static inline uint32_t *stackalloc_u32_allocate(struct StackAllocatorU32 *a, size_t count)
	{
		uint32_t *ret;
		if (count == 0)
			return NULL;
		if (a->offset + count > 1024)
			return NULL;
		ret = a->buffer + a->offset;
		a->offset += count;
		return ret;
	}

	static inline uint32_t *stackalloc_u32_allocate_cleared(struct StackAllocatorU32 *a, size_t count)
	{
		uint32_t *ret = stackalloc_u32_allocate(a, count);
		if (ret)
		{
			size_t i;
			for (i = 0; i < count; i++)
				ret[i] = 0;
		}
		return ret;
	}

	static VkAttachmentLoadOp rp_color_load_op(const RenderPassInfo &info, unsigned index)
	{
		if ((info.clear_attachments & (1u << index)) != 0)
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		else if ((info.load_attachments & (1u << index)) != 0)
			return VK_ATTACHMENT_LOAD_OP_LOAD;
		else
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	static VkAttachmentStoreOp rp_color_store_op(const RenderPassInfo &info, unsigned index)
	{
		if ((info.store_attachments & (1u << index)) != 0)
			return VK_ATTACHMENT_STORE_OP_STORE;
		else
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}

	POD_VEC_DECLARE(VkSubpassDescriptionVec, VkSubpassDescription);
	POD_VEC_DECLARE(VkSubpassDependencyVec, VkSubpassDependency);

	static VkAttachmentReference *rp_find_color(VkSubpassDescription *subpasses,
			unsigned subpass, unsigned attachment)
	{
		const VkAttachmentReference *colors = subpasses[subpass].pColorAttachments;
		for (unsigned i = 0; i < subpasses[subpass].colorAttachmentCount; i++)
			if (colors[i].attachment == attachment)
				return (VkAttachmentReference *)(&colors[i]);
		return NULL;
	}

	static VkAttachmentReference *rp_find_resolve(VkSubpassDescription *subpasses,
			unsigned subpass, unsigned attachment)
	{
		if (!subpasses[subpass].pResolveAttachments)
			return NULL;

		const VkAttachmentReference *resolves = subpasses[subpass].pResolveAttachments;
		for (unsigned i = 0; i < subpasses[subpass].colorAttachmentCount; i++)
			if (resolves[i].attachment == attachment)
				return (VkAttachmentReference *)(&resolves[i]);
		return NULL;
	}

	static VkAttachmentReference *rp_find_input(VkSubpassDescription *subpasses,
			unsigned subpass, unsigned attachment)
	{
		const VkAttachmentReference *inputs = subpasses[subpass].pInputAttachments;
		for (unsigned i = 0; i < subpasses[subpass].inputAttachmentCount; i++)
			if (inputs[i].attachment == attachment)
				return (VkAttachmentReference *)(&inputs[i]);
		return NULL;
	}

	static VkAttachmentReference *rp_find_depth_stencil(VkSubpassDescription *subpasses,
			unsigned subpass, unsigned attachment)
	{
		if (subpasses[subpass].pDepthStencilAttachment->attachment == attachment)
			return (VkAttachmentReference *)(subpasses[subpass].pDepthStencilAttachment);
		else
			return NULL;
	}

	void render_pass_init(struct RenderPass *self, Hash hash, Device *device, const RenderPassInfo &info)
	{
		self->device = device;
		self->render_pass = VK_NULL_HANDLE;
		self->depth_stencil = VK_FORMAT_UNDEFINED;
		self->subpasses.items = NULL; self->subpasses.count = 0; self->subpasses.cap = 0;
		self->intrusive_node.key = hash;
		for (unsigned att = 0; att < VULKAN_NUM_ATTACHMENTS; att++)
			self->color_attachments[att] = VK_FORMAT_UNDEFINED;

		VK_ASSERT(info.num_color_attachments || info.depth_stencil);

		// Want to make load/store to transient a very explicit thing to do, since it will kill performance.
		bool enable_transient_store = (info.op_flags & RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT) != 0;
		bool enable_transient_load = (info.op_flags & RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT) != 0;

		// Set up default subpass info structure if we don't have it.
		const RenderPassInfo::Subpass *subpass_infos = info.subpasses;
		unsigned num_subpasses = info.num_subpasses;
		RenderPassInfo::Subpass default_subpass_info;
		if (!info.subpasses)
		{
			default_subpass_info.num_color_attachments = info.num_color_attachments;
			if (info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT)
				default_subpass_info.depth_stencil_mode = RenderPassInfo::DepthStencil_ReadOnly;
			else
				default_subpass_info.depth_stencil_mode = RenderPassInfo::DepthStencil_ReadWrite;

			for (unsigned i = 0; i < info.num_color_attachments; i++)
				default_subpass_info.color_attachments[i] = i;
			num_subpasses = 1;
			subpass_infos = &default_subpass_info;
		}

		// First, set up attachment descriptions.
		const unsigned num_attachments = info.num_color_attachments + (info.depth_stencil ? 1 : 0);
		VkAttachmentDescription attachments[VULKAN_NUM_ATTACHMENTS + 1];
		uint32_t implicit_transitions = 0;

		VkAttachmentLoadOp ds_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp ds_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		VK_ASSERT(!(info.clear_attachments & info.load_attachments));

		if (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT)
			ds_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
		else if (info.op_flags & RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT)
			ds_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

		if (info.op_flags & RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT)
			ds_store_op = VK_ATTACHMENT_STORE_OP_STORE;

		bool ds_read_only = (info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_READ_ONLY_BIT) != 0;
		VkImageLayout depth_stencil_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (info.depth_stencil)
		{
			depth_stencil_layout = image_get_layout(&imageview_get_image(info.depth_stencil), 
					ds_read_only ?
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL :
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		}

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			self->color_attachments[i] = imageview_get_format(info.color_attachments[i]);
			Image &image = imageview_get_image(info.color_attachments[i]);
			VkAttachmentDescription &att = attachments[i];
			att.flags = 0;
			att.format = self->color_attachments[i];
			att.samples = image_get_create_info(&image).samples;
			att.loadOp = rp_color_load_op(info, i);
			att.storeOp = rp_color_store_op(info, i);
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			// Undefined final layout here for now means that we will just use the layout of the last
			// subpass which uses this attachment to avoid any dummy transition at the end.
			att.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			if (image_get_create_info(&image).domain == ImageDomain_Transient)
			{
				if (enable_transient_load)
				{
					// The transient will behave like a normal image.
					att.initialLayout = image_get_layout(&imageview_get_image(info.color_attachments[i]), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				}
				else
				{
					// Force a clean discard.
					VK_ASSERT(att.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD);
					att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				}

				if (!enable_transient_store)
					att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

				implicit_transitions |= 1u << i;
			}
			else
				att.initialLayout = image_get_layout(&imageview_get_image(info.color_attachments[i]), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}

		self->depth_stencil = info.depth_stencil ? imageview_get_format(info.depth_stencil) : VK_FORMAT_UNDEFINED;
		if (info.depth_stencil)
		{
			Image &image = imageview_get_image(info.depth_stencil);
			VkAttachmentDescription &att = attachments[info.num_color_attachments];
			att.flags = 0;
			att.format = self->depth_stencil;
			att.samples = image_get_create_info(&image).samples;
			att.loadOp = ds_load_op;
			att.storeOp = ds_store_op;
			// Undefined final layout here for now means that we will just use the layout of the last
			// subpass which uses this attachment to avoid any dummy transition at the end.
			att.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			if (format_to_aspect_mask(self->depth_stencil) & VK_IMAGE_ASPECT_STENCIL_BIT)
			{
				att.stencilLoadOp = ds_load_op;
				att.stencilStoreOp = ds_store_op;
			}
			else
			{
				att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			}

			if (image_get_create_info(&image).domain == ImageDomain_Transient)
			{
				if (enable_transient_load)
				{
					// The transient will behave like a normal image.
					att.initialLayout = depth_stencil_layout;
				}
				else
				{
					if (att.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
						att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
					if (att.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
						att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

					// For transient attachments we force the layouts.
					att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				}

				if (!enable_transient_store)
				{
					att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
					att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				}

				implicit_transitions |= 1u << info.num_color_attachments;
			}
			else
				att.initialLayout = depth_stencil_layout;
		}

		struct StackAllocatorRef reference_allocator;
		struct StackAllocatorU32 preserve_allocator;
		reference_allocator.offset = 0;
		preserve_allocator.offset = 0;

		VkSubpassDescriptionVec subpasses = { NULL, 0, 0 };
		{
			VkSubpassDescription zero_sp;
			memset(&zero_sp, 0, sizeof(zero_sp));
			for (unsigned sp = 0; sp < num_subpasses; sp++)
				{ VkSubpassDescription _sp = zero_sp; VkSubpassDescriptionVec_push(&subpasses, &_sp); }
		}
		VkSubpassDependencyVec external_dependencies = { NULL, 0, 0 };
		for (unsigned i = 0; i < num_subpasses; i++)
		{
			VkAttachmentReference *colors = stackalloc_ref_allocate_cleared(&reference_allocator, subpass_infos[i].num_color_attachments);
			VkAttachmentReference *inputs = stackalloc_ref_allocate_cleared(&reference_allocator, subpass_infos[i].num_input_attachments);
			VkAttachmentReference *resolves = stackalloc_ref_allocate_cleared(&reference_allocator, subpass_infos[i].num_color_attachments);
			VkAttachmentReference *depth = stackalloc_ref_allocate_cleared(&reference_allocator, 1);

			VkSubpassDescription &subpass = *VkSubpassDescriptionVec_at(&subpasses, i);
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = subpass_infos[i].num_color_attachments;
			subpass.pColorAttachments = colors;
			subpass.inputAttachmentCount = subpass_infos[i].num_input_attachments;
			subpass.pInputAttachments = inputs;
			subpass.pDepthStencilAttachment = depth;

			if (subpass_infos[i].num_resolve_attachments)
			{
				VK_ASSERT(subpass_infos[i].num_color_attachments == subpass_infos[i].num_resolve_attachments);
				subpass.pResolveAttachments = resolves;
			}

			for (unsigned j = 0; j < subpass.colorAttachmentCount; j++)
			{
				uint32_t att = subpass_infos[i].color_attachments[j];
				VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
				colors[j].attachment = att;
				// Fill in later.
				colors[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			for (unsigned j = 0; j < subpass.inputAttachmentCount; j++)
			{
				uint32_t att = subpass_infos[i].input_attachments[j];
				VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
				inputs[j].attachment = att;
				// Fill in later.
				inputs[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}

			if (subpass.pResolveAttachments)
			{
				for (unsigned j = 0; j < subpass.colorAttachmentCount; j++)
				{
					uint32_t att = subpass_infos[i].resolve_attachments[j];
					VK_ASSERT(att == VK_ATTACHMENT_UNUSED || (att < num_attachments));
					resolves[j].attachment = att;
					// Fill in later.
					resolves[j].layout = VK_IMAGE_LAYOUT_UNDEFINED;
				}
			}

			if (info.depth_stencil && subpass_infos[i].depth_stencil_mode != RenderPassInfo::DepthStencil_None)
			{
				depth->attachment = info.num_color_attachments;
				// Fill in later.
				depth->layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			else
			{
				depth->attachment = VK_ATTACHMENT_UNUSED;
				depth->layout = VK_IMAGE_LAYOUT_UNDEFINED;
			}
		}

		// Now, figure out how each attachment is used throughout the subpasses.
		// Either we don't care (inherit previous pass), or we need something specific.
		// Start with initial layouts.
		uint32_t preserve_masks[VULKAN_NUM_ATTACHMENTS + 1] = {};

		// Last subpass which makes use of an attachment.
		unsigned last_subpass_for_attachment[VULKAN_NUM_ATTACHMENTS + 1] = {};

		VK_ASSERT(num_subpasses <= 32);

		// 1 << subpass bit set if there are color attachment self-dependencies in the subpass.
		uint32_t color_self_dependencies = 0;
		// 1 << subpass bit set if there are depth-stencil attachment self-dependencies in the subpass.
		uint32_t depth_self_dependencies = 0;

		// 1 << subpass bit set if any input attachment is read in the subpass.
		uint32_t input_attachment_read = 0;
		uint32_t color_attachment_read_write = 0;
		uint32_t depth_stencil_attachment_write = 0;
		uint32_t depth_stencil_attachment_read = 0;

		uint32_t external_color_dependencies = 0;
		uint32_t external_depth_dependencies = 0;
		uint32_t external_input_dependencies = 0;

		for (unsigned attachment = 0; attachment < num_attachments; attachment++)
		{
			bool used = false;
			VkImageLayout current_layout = attachments[attachment].initialLayout;
			for (unsigned subpass = 0; subpass < num_subpasses; subpass++)
			{
				VkAttachmentReference *color = rp_find_color(VkSubpassDescriptionVec_data(&subpasses), subpass, attachment);
				VkAttachmentReference *resolve = rp_find_resolve(VkSubpassDescriptionVec_data(&subpasses), subpass, attachment);
				VkAttachmentReference *input = rp_find_input(VkSubpassDescriptionVec_data(&subpasses), subpass, attachment);
				VkAttachmentReference *depth = rp_find_depth_stencil(VkSubpassDescriptionVec_data(&subpasses), subpass, attachment);

				// Sanity check.
				if (color || resolve)
					VK_ASSERT(!depth);
				if (depth)
					VK_ASSERT(!color && !resolve);
				if (resolve)
					VK_ASSERT(!color && !depth);

				if (!color && !input && !depth && !resolve)
				{
					if (used)
						preserve_masks[attachment] |= 1u << subpass;
					continue;
				}

				if (!used && (implicit_transitions & (1u << attachment)))
				{
					// This is the first subpass we need implicit transitions.
					if (color)
						external_color_dependencies |= 1u << subpass;
					if (depth)
						external_depth_dependencies |= 1u << subpass;
					if (input)
						external_input_dependencies |= 1u << subpass;
				}

				if (resolve && input) // If used as both resolve attachment and input attachment in same subpass, need GENERAL.
				{
					current_layout = VK_IMAGE_LAYOUT_GENERAL;
					resolve->layout = current_layout;
					input->layout = current_layout;

					// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
					if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
						attachments[attachment].initialLayout = current_layout;

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
					{
						external_color_dependencies |= 1u << subpass;
						external_input_dependencies |= 1u << subpass;
					}

					used = true;
					last_subpass_for_attachment[attachment] = subpass;

					color_attachment_read_write |= 1u << subpass;
					input_attachment_read |= 1u << subpass;
				}
				else if (resolve)
				{
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
						external_color_dependencies |= 1u << subpass;

					resolve->layout = current_layout;
					used = true;
					last_subpass_for_attachment[attachment] = subpass;
					color_attachment_read_write |= 1u << subpass;
				}
				else if (color && input) // If used as both input attachment and color attachment in same subpass, need GENERAL.
				{
					current_layout = VK_IMAGE_LAYOUT_GENERAL;
					color->layout = current_layout;
					input->layout = current_layout;

					// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
					if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
						attachments[attachment].initialLayout = current_layout;

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
					{
						external_color_dependencies |= 1u << subpass;
						external_input_dependencies |= 1u << subpass;
					}

					used = true;
					last_subpass_for_attachment[attachment] = subpass;
					color_self_dependencies |= 1u << subpass;

					color_attachment_read_write |= 1u << subpass;
					input_attachment_read |= 1u << subpass;
				}
				else if (color) // No particular preference
				{
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
					color->layout = current_layout;

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
						external_color_dependencies |= 1u << subpass;

					used = true;
					last_subpass_for_attachment[attachment] = subpass;
					color_attachment_read_write |= 1u << subpass;
				}
				else if (depth && input) // Depends on the depth mode
				{
					VK_ASSERT(subpass_infos[subpass].depth_stencil_mode != RenderPassInfo::DepthStencil_None);
					if (subpass_infos[subpass].depth_stencil_mode == RenderPassInfo::DepthStencil_ReadWrite)
					{
						depth_self_dependencies |= 1u << subpass;
						current_layout = VK_IMAGE_LAYOUT_GENERAL;
						depth_stencil_attachment_write |= 1u << subpass;

						// If the attachment is first used as a feedback attachment, the initial layout should actually be GENERAL.
						if (!used && attachments[attachment].initialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
							attachments[attachment].initialLayout = current_layout;
					}
					else
					{
						if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
							current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					}

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
					{
						external_input_dependencies |= 1u << subpass;
						external_depth_dependencies |= 1u << subpass;
					}

					depth_stencil_attachment_read |= 1u << subpass;
					input_attachment_read |= 1u << subpass;
					depth->layout = current_layout;
					input->layout = current_layout;
					used = true;
					last_subpass_for_attachment[attachment] = subpass;
				}
				else if (depth)
				{
					if (subpass_infos[subpass].depth_stencil_mode == RenderPassInfo::DepthStencil_ReadWrite)
					{
						depth_stencil_attachment_write |= 1u << subpass;
						if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
							current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					}
					else
					{
						if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
							current_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					}

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
						external_depth_dependencies |= 1u << subpass;

					depth_stencil_attachment_read |= 1u << subpass;
					depth->layout = current_layout;
					used = true;
					last_subpass_for_attachment[attachment] = subpass;
				}
				else if (input)
				{
					if (current_layout != VK_IMAGE_LAYOUT_GENERAL)
						current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					// If the attachment is first used as an input attachment, the initial layout should actually be
					// SHADER_READ_ONLY_OPTIMAL.
					if (!used && attachments[attachment].initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
						attachments[attachment].initialLayout = current_layout;

					// If first subpass changes the layout, we'll need to inject an external subpass dependency.
					if (!used && attachments[attachment].initialLayout != current_layout)
						external_input_dependencies |= 1u << subpass;

					input->layout = current_layout;
					used = true;
					last_subpass_for_attachment[attachment] = subpass;
				}
				else
				{
					VK_ASSERT(0 && "Unhandled attachment usage.");
				}
			}

			// If we don't have a specific layout we need to end up in, just
			// use the last one.
			// Assert that we actually use all the attachments we have ...
			VK_ASSERT(used);
			if (attachments[attachment].finalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
			{
				VK_ASSERT(current_layout != VK_IMAGE_LAYOUT_UNDEFINED);
				attachments[attachment].finalLayout = current_layout;
			}
		}

		// Only consider preserve masks before last subpass which uses an attachment.
		for (unsigned attachment = 0; attachment < num_attachments; attachment++)
			preserve_masks[attachment] &= (1u << last_subpass_for_attachment[attachment]) - 1;

		// Add preserve attachments as needed.
		for (unsigned subpass = 0; subpass < num_subpasses; subpass++)
		{
			VkSubpassDescription &pass = *VkSubpassDescriptionVec_at(&subpasses, subpass);
			unsigned preserve_count = 0;
			for (unsigned attachment = 0; attachment < num_attachments; attachment++)
				if (preserve_masks[attachment] & (1u << subpass))
					preserve_count++;

			uint32_t *preserve = stackalloc_u32_allocate_cleared(&preserve_allocator, preserve_count);
			pass.pPreserveAttachments = preserve;
			pass.preserveAttachmentCount = preserve_count;
			for (unsigned attachment = 0; attachment < num_attachments; attachment++)
				if (preserve_masks[attachment] & (1u << subpass))
					*preserve++ = attachment;
		}

		VK_ASSERT(num_subpasses > 0);
		VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		rp_info.subpassCount = num_subpasses;
		rp_info.pSubpasses = VkSubpassDescriptionVec_data(&subpasses);
		rp_info.pAttachments = attachments;
		rp_info.attachmentCount = num_attachments;

		// Add external subpass dependencies.
		FOR_EACH_BIT(external_color_dependencies | external_depth_dependencies | external_input_dependencies, subpass)
		{
			{ VkSubpassDependency zero_dep; memset(&zero_dep, 0, sizeof(zero_dep)); { VkSubpassDependency _d = zero_dep; VkSubpassDependencyVec_push(&external_dependencies, &_d); } }
			VkSubpassDependency &dep = *VkSubpassDependencyVec_back(&external_dependencies);
			dep.srcSubpass = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass = subpass;

			if (external_color_dependencies & (1u << subpass))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dep.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				dep.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}

			if (external_depth_dependencies & (1u << subpass))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			}

			if (external_input_dependencies & (1u << subpass))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				dep.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			}
		}

		// Queue up self-dependencies (COLOR | DEPTH) -> INPUT.
		FOR_EACH_BIT(color_self_dependencies | depth_self_dependencies, subpass)
		{
			{ VkSubpassDependency zero_dep; memset(&zero_dep, 0, sizeof(zero_dep)); { VkSubpassDependency _d = zero_dep; VkSubpassDependencyVec_push(&external_dependencies, &_d); } }
			VkSubpassDependency &dep = *VkSubpassDependencyVec_back(&external_dependencies);
			dep.srcSubpass = subpass;
			dep.dstSubpass = subpass;
			dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			if (color_self_dependencies & (1u << subpass))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}

			if (depth_self_dependencies & (1u << subpass))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}

			dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}

		// Flush and invalidate caches between each subpass.
		for (unsigned subpass = 1; subpass < num_subpasses; subpass++)
		{
			{ VkSubpassDependency zero_dep; memset(&zero_dep, 0, sizeof(zero_dep)); { VkSubpassDependency _d = zero_dep; VkSubpassDependencyVec_push(&external_dependencies, &_d); } }
			VkSubpassDependency &dep = *VkSubpassDependencyVec_back(&external_dependencies);
			dep.srcSubpass = subpass - 1;
			dep.dstSubpass = subpass;
			dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			if (color_attachment_read_write & (1u << (subpass - 1)))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dep.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			}

			if (depth_stencil_attachment_write & (1u << (subpass - 1)))
			{
				dep.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			}

			if (color_attachment_read_write & (1u << subpass))
			{
				dep.dstStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				dep.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			}

			if (depth_stencil_attachment_read & (1u << subpass))
			{
				dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			}

			if (depth_stencil_attachment_write & (1u << subpass))
			{
				dep.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			}

			if (input_attachment_read & (1u << subpass))
			{
				dep.dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				dep.dstAccessMask |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			}
		}

		if (!VkSubpassDependencyVec_empty(&external_dependencies))
		{
			rp_info.dependencyCount = VkSubpassDependencyVec_size(&external_dependencies);
			rp_info.pDependencies = VkSubpassDependencyVec_data(&external_dependencies);
		}

		// Store the important subpass information for later.
		for (uint32_t subpass_idx = 0; subpass_idx < rp_info.subpassCount; subpass_idx++)
		{
			const VkSubpassDescription &subpass = rp_info.pSubpasses[subpass_idx];

			RenderPassSubpassInfo subpass_info = {};
			/* Defensive bounds: the internal RenderPassInfo builder always stays
			 * within VULKAN_NUM_ATTACHMENTS and sets a non-NULL depth-stencil
			 * pointer, but clamp and null-check here so a stray/large subpass can
			 * never overrun the fixed-size arrays or deref NULL. */
			subpass_info.num_color_attachments = subpass.colorAttachmentCount;
			subpass_info.num_input_attachments = subpass.inputAttachmentCount;
			VK_ASSERT(subpass.colorAttachmentCount <= VULKAN_NUM_ATTACHMENTS);
			VK_ASSERT(subpass.inputAttachmentCount <= VULKAN_NUM_ATTACHMENTS);
			if (subpass_info.num_color_attachments > VULKAN_NUM_ATTACHMENTS)
				subpass_info.num_color_attachments = VULKAN_NUM_ATTACHMENTS;
			if (subpass_info.num_input_attachments > VULKAN_NUM_ATTACHMENTS)
				subpass_info.num_input_attachments = VULKAN_NUM_ATTACHMENTS;
			if (subpass.pDepthStencilAttachment)
				subpass_info.depth_stencil_attachment = *subpass.pDepthStencilAttachment;
			else
			{
				subpass_info.depth_stencil_attachment.attachment = VK_ATTACHMENT_UNUSED;
				subpass_info.depth_stencil_attachment.layout     = VK_IMAGE_LAYOUT_UNDEFINED;
			}
			if (subpass_info.num_color_attachments)
				memcpy(subpass_info.color_attachments, subpass.pColorAttachments,
						subpass_info.num_color_attachments * sizeof(*subpass.pColorAttachments));
			if (subpass_info.num_input_attachments)
				memcpy(subpass_info.input_attachments, subpass.pInputAttachments,
						subpass_info.num_input_attachments * sizeof(*subpass.pInputAttachments));

			unsigned samples = 0;
			for (unsigned i = 0; i < subpass_info.num_color_attachments; i++)
			{
				if (subpass_info.color_attachments[i].attachment == VK_ATTACHMENT_UNUSED)
					continue;

				unsigned samp = attachments[subpass_info.color_attachments[i].attachment].samples;
				VK_ASSERT(!samples || samp == samples);
				samples = samp;
			}

			if (subpass_info.depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED)
			{
				unsigned samp = attachments[subpass_info.depth_stencil_attachment.attachment].samples;
				VK_ASSERT(!samples || samp == samples);
				samples = samp;
			}

			VK_ASSERT(samples > 0);
			subpass_info.samples = samples;
			SubpassInfoVec_push(&self->subpasses, &subpass_info);
		}


		// Fixup after, we want the underlying render pass to be generic.
		VkAttachmentDescription fixup_attachments[VULKAN_NUM_ATTACHMENTS + 1];
		render_pass_fixup_render_pass_nvidia(self, rp_info, fixup_attachments);

		LOGI("Creating render pass.\n");
		if (vkCreateRenderPass(device->get_device(), &rp_info, NULL, &self->render_pass) != VK_SUCCESS)
			LOGE("Failed to create render pass.");

		VkSubpassDescriptionVec_free_storage(&subpasses);
		VkSubpassDependencyVec_free_storage(&external_dependencies);
	}

	static void render_pass_fixup_render_pass_nvidia(struct RenderPass *self, VkRenderPassCreateInfo &create_info, VkAttachmentDescription *attachments)
	{
		if (self->device->get_gpu_properties().vendorID == VENDOR_ID_NVIDIA &&
#ifdef _WIN32
				VK_VERSION_MAJOR(self->device->get_gpu_properties().driverVersion) < 417)
#else
			VK_VERSION_MAJOR(self->device->get_gpu_properties().driverVersion) < 415)
#endif
			{
				// Workaround a bug on NV where depth-stencil input attachments break if we have STORE_OP_DONT_CARE.
				// Force STORE_OP_STORE for all attachments.
				if (attachments != create_info.pAttachments)
				{
					memcpy(attachments, create_info.pAttachments, create_info.attachmentCount * sizeof(attachments[0]));
					create_info.pAttachments = attachments;
				}

				for (uint32_t i = 0; i < create_info.attachmentCount; i++)
				{
					VkFormat format = attachments[i].format;
					VkImageAspectFlags aspect = format_to_aspect_mask(format);
					if ((aspect & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT)) != 0)
						attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
					if ((aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
						attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
				}
			}
	}

	void render_pass_fini(struct RenderPass *self)
	{
		if (self->render_pass != VK_NULL_HANDLE)
			vkDestroyRenderPass(self->device->get_device(), self->render_pass, NULL);
		SubpassInfoVec_free_storage(&self->subpasses);
	}

	void framebuffer_init(struct Framebuffer *self, Device *device, const RenderPass *rp, const RenderPassInfo &info)
	{
		VkImageView views[VULKAN_NUM_ATTACHMENTS + 1];
		unsigned num_views = 0;
		self->device = device;
		self->framebuffer = VK_NULL_HANDLE;
		self->render_pass = rp;
		self->info = info;
		self->num_attachments = 0;
		cookie_init(&self->cookie_base, device);
		self->width = UINT32_MAX;
		self->height = UINT32_MAX;

		VK_ASSERT(info.num_color_attachments || info.depth_stencil);

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			auto *att = info.color_attachments[i];
			unsigned lod = imageview_get_create_info(att).base_level;
			unsigned aw  = image_get_width(&imageview_get_image(att), lod);
			unsigned ah  = image_get_height(&imageview_get_image(att), lod);
			if (aw < self->width)  self->width  = aw;
			if (ah < self->height) self->height = ah;
			views[num_views++] = imageview_get_render_target_view(att, info.layer);
			self->attachments[self->num_attachments++] = att;
		}

		if (info.depth_stencil)
		{
			auto *att = info.depth_stencil;
			unsigned lod = imageview_get_create_info(att).base_level;
			unsigned aw  = image_get_width(&imageview_get_image(att), lod);
			unsigned ah  = image_get_height(&imageview_get_image(att), lod);
			if (aw < self->width)  self->width  = aw;
			if (ah < self->height) self->height = ah;
			views[num_views++] = imageview_get_render_target_view(att, info.layer);
			self->attachments[self->num_attachments++] = att;
		}

		VkFramebufferCreateInfo fb_info = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		fb_info.renderPass = render_pass_get_render_pass(rp);
		fb_info.attachmentCount = num_views;
		fb_info.pAttachments = views;
		fb_info.width = self->width;
		fb_info.height = self->height;
		fb_info.layers = 1;

		if (vkCreateFramebuffer(device->get_device(), &fb_info, NULL, &self->framebuffer) != VK_SUCCESS)
			LOGE("Failed to create framebuffer.");
	}

	void framebuffer_fini(struct Framebuffer *self)
	{
		if (self->framebuffer != VK_NULL_HANDLE)
			self->device->destroy_framebuffer_nolock(self->framebuffer);
	}

	void framebuffer_allocator_clear(struct FramebufferAllocator *self)
	{
		framebuffer_thmap_clear(&self->framebuffers);
	}

	void framebuffer_allocator_begin_frame(struct FramebufferAllocator *self)
	{
		framebuffer_thmap_begin_frame(&self->framebuffers);
	}

	struct Framebuffer *framebuffer_allocator_request_framebuffer(struct FramebufferAllocator *self, const RenderPassInfo &info)
	{
		const RenderPass &rp = self->device->request_render_pass(info, true);
		Hash hash;
		FramebufferNode *node;
		Hasher h; hasher_init(&h);
		hasher_u64(&h, rp.intrusive_node.key);

		for (unsigned i = 0; i < info.num_color_attachments; i++)
			if (info.color_attachments[i])
				hasher_u64(&h, info.color_attachments[i]->cookie_base.cookie);

		if (info.depth_stencil)
			hasher_u64(&h, info.depth_stencil->cookie_base.cookie);

		hasher_u32(&h, info.layer);

		hash = hasher_get(&h);

		node = framebuffer_thmap_request(&self->framebuffers, hash);
		if (node)
			return &node->base;

		return &framebuffer_thmap_emplace(&self->framebuffers, hash, self->device, &rp, info)->base;
	}

	void attachment_allocator_clear(struct AttachmentAllocator *self)
	{
		transient_thmap_clear(&self->attachments);
	}

	void attachment_allocator_begin_frame(struct AttachmentAllocator *self)
	{
		transient_thmap_begin_frame(&self->attachments);
	}

	struct ImageView *attachment_allocator_request_attachment(struct AttachmentAllocator *self, unsigned width, unsigned height, VkFormat format,
			unsigned index, unsigned samples, unsigned layers)
	{
		Hash hash;
		TransientNode *node;
		ImageCreateInfo image_info;
		Hasher h; hasher_init(&h);
		hasher_u32(&h, width);
		hasher_u32(&h, height);
		hasher_u32(&h, format);
		hasher_u32(&h, index);
		hasher_u32(&h, samples);
		hasher_u32(&h, layers);

		hash = hasher_get(&h);

		node = transient_thmap_request(&self->attachments, hash);
		if (node)
			return &image_get_view(ih_get(&node->handle));

		image_info = ImageCreateInfo::transient_render_target(width, height, format);

		image_info.samples = (VkSampleCountFlagBits)(samples);
		image_info.layers = layers;
		node = transient_thmap_emplace(&self->attachments, hash, self->device->create_image(image_info, NULL));
		self->device->set_name(*ih_get(&node->handle), "AttachmentAllocator");
		return &image_get_view(ih_get(&node->handle));
	}

/* === command_buffer.cpp === */


#define COMBINER_NEEDS_BLEND_CONSTANT(factor) ((factor) == VK_BLEND_FACTOR_CONSTANT_COLOR || (factor) == VK_BLEND_FACTOR_CONSTANT_ALPHA)

	static inline VkOffset3D cb_add_offset(const VkOffset3D &a, const VkOffset3D &b)
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	void commandbuffer_init(struct CommandBuffer *self, Device *device, VkCommandBuffer cmd, CommandBufferType type)
	{
		self->device = device;
		self->cmd    = cmd;
		self->type   = type;
		/* Members that previously had default initializers in the class body. */
		self->framebuffer             = NULL;
		self->actual_render_pass      = NULL;
		self->compatible_render_pass  = NULL;
		memset(self->attribs, 0, sizeof(self->attribs));
		memset(&self->vbo, 0, sizeof(self->vbo));
		self->current_pipeline        = VK_NULL_HANDLE;
		self->current_pipeline_layout = VK_NULL_HANDLE;
		self->current_layout          = NULL;
		self->current_program         = NULL;
		self->current_subpass         = 0;
		self->current_contents        = VK_SUBPASS_CONTENTS_INLINE;
		memset(&self->viewport, 0, sizeof(self->viewport));
		memset(&self->scissor, 0, sizeof(self->scissor));
		self->dirty       = ~0u;
		self->dirty_sets  = 0;
		self->dirty_vbos  = 0;
		self->active_vbos = 0;
		self->is_compute  = true;
		memset(&self->potential_static_state, 0, sizeof(self->potential_static_state));

		/* vbo_block/ubo_block are plain BufferBlock structs now; the old default
		 * ctor zeroed them, so init explicitly. */
		bufferblock_init(&self->vbo_block);
		bufferblock_init(&self->ubo_block);

		counter_init(&self->reference_count); /* refcount starts at 1 */
		commandbuffer_begin_compute(self);
		commandbuffer_set_opaque_state(self);
		memset(&self->static_state, 0, sizeof(self->static_state));
		memset(&self->bindings, 0, sizeof(self->bindings));
	}

	void commandbuffer_fini(struct CommandBuffer *self)
	{
		VK_ASSERT(self->vbo_block.mapped == NULL);
		VK_ASSERT(self->ubo_block.mapped == NULL);
		/* The blocks are normally drained back to their pools (handles NULL) before
		 * teardown; fini is a safe release either way (was ~BufferBlock). */
		bufferblock_fini(&self->vbo_block);
		bufferblock_fini(&self->ubo_block);
	}

	void commandbuffer_add_reference(struct CommandBuffer *self)
	{
		counter_add_ref(&self->reference_count);
	}

	void commandbuffer_release_reference(struct CommandBuffer *self)
	{
		if (counter_release(&self->reference_count))
			CommandBufferDeleter()(self);
	}

	void commandbuffer_copy_buffer(struct CommandBuffer *self, const Buffer &dst, VkDeviceSize dst_offset, const Buffer &src, VkDeviceSize src_offset,
			VkDeviceSize size)
	{
		const VkBufferCopy region = {
			src_offset, dst_offset, size,
		};
		vkCmdCopyBuffer(self->cmd, buffer_get_buffer(&src), buffer_get_buffer(&dst), 1, &region);
	}

	void commandbuffer_copy_buffer_to_image_blits(struct CommandBuffer *self, const Image &image, const Buffer &buffer, unsigned num_blits,
			const VkBufferImageCopy *blits)
	{
		vkCmdCopyBufferToImage(self->cmd, buffer_get_buffer(&buffer),
				image_get_image(&image), image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), num_blits, blits);
	}

	void commandbuffer_copy_buffer_to_image(struct CommandBuffer *self, const Image &image, const Buffer &src, VkDeviceSize buffer_offset,
			const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
			unsigned slice_height, const VkImageSubresourceLayers &subresource)
	{
		const VkBufferImageCopy region = {
			buffer_offset,
			row_length != extent.width ? row_length : 0, slice_height != extent.height ? slice_height : 0,
			subresource, offset, extent,
		};
		vkCmdCopyBufferToImage(self->cmd, buffer_get_buffer(&src), image_get_image(&image), image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
				1, &region);
	}

	void commandbuffer_copy_image_to_buffer(struct CommandBuffer *self, const Buffer &buffer, const Image &image, VkDeviceSize buffer_offset,
			const VkOffset3D &offset, const VkExtent3D &extent, unsigned row_length,
			unsigned slice_height, const VkImageSubresourceLayers &subresource)
	{
		const VkBufferImageCopy region = {
			buffer_offset,
			row_length != extent.width ? row_length : 0, slice_height != extent.height ? slice_height : 0,
			subresource, offset, extent,
		};
		vkCmdCopyImageToBuffer(self->cmd, image_get_image(&image), image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
				buffer_get_buffer(&buffer), 1, &region);
	}

	void commandbuffer_clear_image(struct CommandBuffer *self, const Image &image, const VkClearValue &value)
	{
		VK_ASSERT(!self->framebuffer);
		VK_ASSERT(!self->actual_render_pass);

		VkImageAspectFlags aspect = format_to_aspect_mask(image_get_format(&image));
		VkImageSubresourceRange range = {};
		range.aspectMask = aspect;
		range.baseArrayLayer = 0;
		range.baseMipLevel = 0;
		range.levelCount = image_get_create_info(&image).levels;
		range.layerCount = image_get_create_info(&image).layers;
		if (aspect & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			vkCmdClearColorImage(self->cmd, image_get_image(&image), image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
					&value.color, 1, &range);
		}
		else
		{
			vkCmdClearDepthStencilImage(self->cmd, image_get_image(&image), image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
					&value.depthStencil, 1, &range);
		}
	}

	void commandbuffer_full_barrier(struct CommandBuffer *self)
	{
		VK_ASSERT(!self->actual_render_pass);
		VK_ASSERT(!self->framebuffer);
		commandbuffer_barrier_simple(self, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
	}

	void commandbuffer_pixel_barrier(struct CommandBuffer *self)
	{
		VK_ASSERT(self->actual_render_pass);
		VK_ASSERT(self->framebuffer);
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		vkCmdPipelineBarrier(self->cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0, NULL, 0, NULL);
	}

	static inline void fixup_src_stage(VkPipelineStageFlags &src_stages, bool fixup)
	{
		// ALL_GRAPHICS_BIT waits for vertex as well which causes performance issues on some drivers.
		// It shouldn't matter, but hey.
		//
		// We aren't using vertex with side-effects on relevant hardware so dropping VERTEX_SHADER_BIT is fine.
		if ((src_stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0 && fixup)
		{
			src_stages &= ~VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
			src_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
	}

	void commandbuffer_barrier_simple(struct CommandBuffer *self, VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages,
			VkAccessFlags dst_access)
	{
		VK_ASSERT(!self->actual_render_pass);
		VK_ASSERT(!self->framebuffer);
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = src_access;
		barrier.dstAccessMask = dst_access;
		fixup_src_stage(src_stages, self->device->get_workarounds().optimize_all_graphics_barrier);
		vkCmdPipelineBarrier(self->cmd, src_stages, dst_stages, 0, 1, &barrier, 0, NULL, 0, NULL);
	}

	void commandbuffer_barrier(struct CommandBuffer *self, VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, unsigned barriers,
			const VkMemoryBarrier *globals, unsigned buffer_barriers,
			const VkBufferMemoryBarrier *buffers, unsigned image_barriers,
			const VkImageMemoryBarrier *images)
	{
		VK_ASSERT(!self->actual_render_pass);
		VK_ASSERT(!self->framebuffer);
		fixup_src_stage(src_stages, self->device->get_workarounds().optimize_all_graphics_barrier);
		vkCmdPipelineBarrier(self->cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers, buffers, image_barriers, images);
	}

	void commandbuffer_image_barrier(struct CommandBuffer *self, const Image &image, VkImageLayout old_layout, VkImageLayout new_layout,
			VkPipelineStageFlags src_stages, VkAccessFlags src_access,
			VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
	{
		VK_ASSERT(!self->actual_render_pass);
		VK_ASSERT(!self->framebuffer);
		VK_ASSERT(image_get_create_info(&image).domain != ImageDomain_Transient);

		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.srcAccessMask = src_access;
		barrier.dstAccessMask = dst_access;
		barrier.oldLayout = old_layout;
		barrier.newLayout = new_layout;
		barrier.image = image_get_image(&image);
		barrier.subresourceRange.aspectMask = format_to_aspect_mask(image_get_create_info(&image).format);
		barrier.subresourceRange.levelCount = image_get_create_info(&image).levels;
		barrier.subresourceRange.layerCount = image_get_create_info(&image).layers;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		fixup_src_stage(src_stages, self->device->get_workarounds().optimize_all_graphics_barrier);
		vkCmdPipelineBarrier(self->cmd, src_stages, dst_stages, 0, 0, NULL, 0, NULL, 1, &barrier);
	}

	void commandbuffer_barrier_prepare_generate_mipmap(struct CommandBuffer *self, const Image &image, VkImageLayout base_level_layout,
			VkPipelineStageFlags src_stage, VkAccessFlags src_access,
			bool need_top_level_barrier)
	{
		const ImageCreateInfo &create_info = image_get_create_info(&image);
		VkImageMemoryBarrier barriers[2] = {};
		VK_ASSERT(create_info.levels > 1);
		(void)create_info;

		for (unsigned i = 0; i < 2; i++)
		{
			barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[i].image = image_get_image(&image);
			barriers[i].subresourceRange.aspectMask = format_to_aspect_mask(image_get_format(&image));
			barriers[i].subresourceRange.layerCount = image_get_create_info(&image).layers;
			barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			if (i == 0)
			{
				barriers[i].oldLayout = base_level_layout;
				barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barriers[i].srcAccessMask = src_access;
				barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barriers[i].subresourceRange.baseMipLevel = 0;
				barriers[i].subresourceRange.levelCount = 1;
			}
			else
			{
				barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barriers[i].srcAccessMask = 0;
				barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barriers[i].subresourceRange.baseMipLevel = 1;
				barriers[i].subresourceRange.levelCount = image_get_create_info(&image).levels - 1;
			}
		}

		commandbuffer_barrier(self, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, NULL, 0, NULL,
				need_top_level_barrier ? 2 : 1,
				need_top_level_barrier ? barriers : barriers + 1);
	}

	void commandbuffer_generate_mipmap(struct CommandBuffer *self, const Image &image)
	{
		const ImageCreateInfo &create_info = image_get_create_info(&image);
		VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
		const VkOffset3D origin = { 0, 0, 0 };

		VK_ASSERT(image_get_layout(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.image = image_get_image(&image);
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.layerCount = image_get_create_info(&image).layers;
		b.subresourceRange.aspectMask = format_to_aspect_mask(image_get_format(&image));
		b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		for (unsigned i = 1; i < create_info.levels; i++)
		{
			VkOffset3D src_size = size;
			size.x >>= 1;
			size.y >>= 1;
			size.z >>= 1;
			if (size.x < 1) size.x = 1;
			if (size.y < 1) size.y = 1;
			if (size.z < 1) size.z = 1;

			commandbuffer_blit_image(self, image, image,
					origin, size, origin, src_size, i, i - 1, 0, 0, create_info.layers, VK_FILTER_LINEAR);

			b.subresourceRange.baseMipLevel = i;
			commandbuffer_barrier(self, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					0, NULL, 0, NULL, 1, &b);
		}
	}

	void commandbuffer_blit_image(struct CommandBuffer *self, const Image &dst, const Image &src,
			const VkOffset3D &dst_offset,
			const VkOffset3D &dst_extent, const VkOffset3D &src_offset, const VkOffset3D &src_extent,
			unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
			unsigned num_layers, VkFilter filter)
	{
		// RADV workaround: blit one layer at a time.
		for (unsigned i = 0; i < num_layers; i++)
		{
			const VkImageBlit blit = {
				{ format_to_aspect_mask(image_get_create_info(&src).format), src_level, src_base_layer + i, 1 },
				{ src_offset,                                          cb_add_offset(src_offset, src_extent) },
				{ format_to_aspect_mask(image_get_create_info(&dst).format), dst_level, dst_base_layer + i, 1 },
				{ dst_offset,                                          cb_add_offset(dst_offset, dst_extent) },
			};

			vkCmdBlitImage(self->cmd,
					image_get_image(&src), image_get_layout(&src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
					image_get_image(&dst), image_get_layout(&dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
					1, &blit, filter);
		}
	}

	void commandbuffer_begin_context(struct CommandBuffer *self)
	{
		self->dirty = ~0u;
		self->dirty_sets = ~0u;
		self->dirty_vbos = ~0u;
		self->current_pipeline = VK_NULL_HANDLE;
		self->current_pipeline_layout = VK_NULL_HANDLE;
		self->current_layout = NULL;
		self->current_program = NULL;
		memset(self->bindings.cookies, 0, sizeof(self->bindings.cookies));
		memset(self->bindings.secondary_cookies, 0, sizeof(self->bindings.secondary_cookies));
		memset(self->vbo.buffers, 0, sizeof(self->vbo.buffers));
	}

	void commandbuffer_init_viewport_scissor(struct CommandBuffer *self, const RenderPassInfo &info, const Framebuffer *framebuffer)
	{
		const uint32_t fb_w = framebuffer_get_width(self->framebuffer);
		const uint32_t fb_h = framebuffer_get_height(self->framebuffer);
		VkRect2D rect = info.render_area;
		if (uint32_t(rect.offset.x) > fb_w) rect.offset.x = int32_t(fb_w);
		if (uint32_t(rect.offset.y) > fb_h) rect.offset.y = int32_t(fb_h);
		{
			uint32_t w_avail = fb_w - uint32_t(rect.offset.x);
			uint32_t h_avail = fb_h - uint32_t(rect.offset.y);
			if (w_avail < rect.extent.width)  rect.extent.width  = w_avail;
			if (h_avail < rect.extent.height) rect.extent.height = h_avail;
		}

		self->viewport = { 0.0f, 0.0f, float(fb_w), float(fb_h), 0.0f, 1.0f };
		self->scissor = rect;
	}

	void commandbuffer_begin_render_pass(struct CommandBuffer *self, const RenderPassInfo &info, VkSubpassContents contents)
	{
		VK_ASSERT(!self->framebuffer);
		VK_ASSERT(!self->compatible_render_pass);
		VK_ASSERT(!self->actual_render_pass);

		self->framebuffer = &self->device->request_framebuffer(info);
		self->compatible_render_pass = framebuffer_get_compatible_render_pass(self->framebuffer);
		self->actual_render_pass = &self->device->request_render_pass(info, false);

		commandbuffer_init_viewport_scissor(self, info, self->framebuffer);

		VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
		unsigned num_clear_values = 0;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			if (info.clear_attachments & (1u << i))
			{
				clear_values[i].color = info.clear_color[i];
				num_clear_values = i + 1;
			}
		}

		if (info.depth_stencil && (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT) != 0)
		{
			clear_values[info.num_color_attachments].depthStencil = info.clear_depth_stencil;
			num_clear_values = info.num_color_attachments + 1;
		}

		VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		begin_info.renderPass = render_pass_get_render_pass(self->actual_render_pass);
		begin_info.framebuffer = framebuffer_get_framebuffer(self->framebuffer);
		begin_info.renderArea = self->scissor;
		begin_info.clearValueCount = num_clear_values;
		begin_info.pClearValues = clear_values;

		vkCmdBeginRenderPass(self->cmd, &begin_info, contents);

		self->current_contents = contents;
		commandbuffer_begin_graphics(self);
	}

	void commandbuffer_end_render_pass(struct CommandBuffer *self)
	{
		VK_ASSERT(self->framebuffer);
		VK_ASSERT(self->actual_render_pass);
		VK_ASSERT(self->compatible_render_pass);

		vkCmdEndRenderPass(self->cmd);

		self->framebuffer = NULL;
		self->actual_render_pass = NULL;
		self->compatible_render_pass = NULL;
		commandbuffer_begin_compute(self);
	}

	VkPipeline commandbuffer_build_compute_pipeline(struct CommandBuffer *self, Hash hash)
	{
		const Shader &shader = *program_get_shader(self->current_program, ShaderStage_Compute);
		VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		info.layout = pipeline_layout_get_layout(program_get_pipeline_layout(self->current_program));
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.module = shader_get_module(&shader);
		info.stage.pName = "main";
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

#ifdef GRANITE_SPIRV_DUMP
		LOGI("Compiling SPIR-V file: (%s) %s\n",
				shader_stage_to_name(ShaderStage_Compute),
				(to_string(shader.intrusive_node.key) + ".spv").c_str());
#endif

		VkSpecializationInfo spec_info = {};
		VkSpecializationMapEntry spec_entries[VULKAN_NUM_SPEC_CONSTANTS];
		uint32_t mask = pipeline_layout_get_resource_layout(self->current_layout)->combined_spec_constant_mask &
			self->static_state.state.spec_constant_mask;

		if (mask)
		{
			info.stage.pSpecializationInfo = &spec_info;
			spec_info.pData = self->potential_static_state.spec_constants;
			spec_info.dataSize = sizeof(self->potential_static_state.spec_constants);
			spec_info.pMapEntries = spec_entries;

			FOR_EACH_BIT(mask, bit)
			{
				VkSpecializationMapEntry &entry = spec_entries[spec_info.mapEntryCount++];
				entry.offset = sizeof(uint32_t) * bit;
				entry.size = sizeof(uint32_t);
				entry.constantID = bit;
			}
		}

		VkPipeline compute_pipeline;

		LOGI("Creating compute pipeline.\n");
		if (vkCreateComputePipelines(self->device->get_device(), VK_NULL_HANDLE, 1, &info, NULL, &compute_pipeline) != VK_SUCCESS)
			LOGE("Failed to create compute pipeline!\n");

		return program_add_pipeline(self->current_program, hash, compute_pipeline);
	}

	VkPipeline commandbuffer_build_graphics_pipeline(struct CommandBuffer *self, Hash hash)
	{
		// Viewport state
		VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		// Dynamic state
		VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dyn.dynamicStateCount = 2;
		static const VkDynamicState states[2] = {
			VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
		};
		dyn.pDynamicStates = states;

		// Blend state
		VkPipelineColorBlendAttachmentState blend_attachments[VULKAN_NUM_ATTACHMENTS];
		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = render_pass_get_num_color_attachments(self->compatible_render_pass, self->current_subpass);
		blend.pAttachments = blend_attachments;
		for (unsigned i = 0; i < blend.attachmentCount; i++)
		{
			VkPipelineColorBlendAttachmentState &att = blend_attachments[i];
			att = {};

			if (render_pass_get_color_attachment(self->compatible_render_pass, self->current_subpass, i)->attachment != VK_ATTACHMENT_UNUSED &&
					(pipeline_layout_get_resource_layout(self->current_layout)->render_target_mask & (1u << i)))
			{
				att.colorWriteMask = 0xf;
				att.blendEnable = self->static_state.state.blend_enable;
				if (att.blendEnable)
				{
					att.alphaBlendOp = (VkBlendOp)(self->static_state.state.alpha_blend_op);
					att.colorBlendOp = (VkBlendOp)(self->static_state.state.color_blend_op);
					att.dstAlphaBlendFactor = (VkBlendFactor)(self->static_state.state.dst_alpha_blend);
					att.srcAlphaBlendFactor = (VkBlendFactor)(self->static_state.state.src_alpha_blend);
					att.dstColorBlendFactor = (VkBlendFactor)(self->static_state.state.dst_color_blend);
					att.srcColorBlendFactor = (VkBlendFactor)(self->static_state.state.src_color_blend);
				}
			}
		}
		memcpy(blend.blendConstants, self->potential_static_state.blend_constants, sizeof(blend.blendConstants));

		// Depth state
		VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		ds.depthTestEnable = render_pass_has_depth(self->compatible_render_pass, self->current_subpass) && self->static_state.state.depth_test;
		ds.depthWriteEnable = render_pass_has_depth(self->compatible_render_pass, self->current_subpass) && self->static_state.state.depth_write;

		if (ds.depthTestEnable)
			ds.depthCompareOp = (VkCompareOp)(self->static_state.state.depth_compare);

		// Vertex input
		VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkVertexInputAttributeDescription vi_attribs[VULKAN_NUM_VERTEX_ATTRIBS];
		vi.pVertexAttributeDescriptions = vi_attribs;
		uint32_t attr_mask = pipeline_layout_get_resource_layout(self->current_layout)->attribute_mask;
		uint32_t binding_mask = 0;
		FOR_EACH_BIT(attr_mask, bit)
		{
			VkVertexInputAttributeDescription &attr = vi_attribs[vi.vertexAttributeDescriptionCount++];
			attr.location = bit;
			attr.binding = self->attribs[bit].binding;
			attr.format = self->attribs[bit].format;
			attr.offset = self->attribs[bit].offset;
			binding_mask |= 1u << attr.binding;
		}

		VkVertexInputBindingDescription vi_bindings[VULKAN_NUM_VERTEX_BUFFERS];
		vi.pVertexBindingDescriptions = vi_bindings;
		FOR_EACH_BIT(binding_mask, bit)
		{
			VkVertexInputBindingDescription &bind = vi_bindings[vi.vertexBindingDescriptionCount++];
			bind.binding = bit;
			bind.inputRate = self->vbo.input_rates[bit];
			bind.stride = self->vbo.strides[bit];
		}

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		ia.topology = (VkPrimitiveTopology)(self->static_state.state.topology);

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		ms.rasterizationSamples = (VkSampleCountFlagBits)(render_pass_get_sample_count(self->compatible_render_pass, self->current_subpass));

		if (render_pass_get_sample_count(self->compatible_render_pass, self->current_subpass) > 1)
		{
			ms.alphaToCoverageEnable = self->static_state.state.alpha_to_coverage;
			ms.alphaToOneEnable = self->static_state.state.alpha_to_one;
			ms.sampleShadingEnable = self->static_state.state.sample_shading;
			ms.minSampleShading = 1.0f;
		}

		// Raster
		VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.cullMode = (VkCullModeFlags)(self->static_state.state.cull_mode);
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1.0f;
		raster.polygonMode = VK_POLYGON_MODE_FILL;

		// Stages
		VkPipelineShaderStageCreateInfo stages[(unsigned)(ShaderStage_Count)];
		unsigned num_stages = 0;

		VkSpecializationInfo spec_info[(unsigned)ShaderStage_Count] = {};
		VkSpecializationMapEntry spec_entries[(unsigned)ShaderStage_Count][VULKAN_NUM_SPEC_CONSTANTS];

		for (unsigned i = 0; i < (unsigned)(ShaderStage_Count); i++)
		{
			ShaderStage stage = (ShaderStage)(i);
			if (program_get_shader(self->current_program, stage))
			{
				VkPipelineShaderStageCreateInfo &s = stages[num_stages++];
				s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
				s.module = shader_get_module(program_get_shader(self->current_program, stage));
#ifdef GRANITE_SPIRV_DUMP
				LOGI("Compiling SPIR-V file: (%s) %s\n",
						shader_stage_to_name(stage),
						(to_string(program_get_shader(self->current_program, stage)->intrusive_node.key) + ".spv").c_str());
#endif
				s.pName = "main";
				s.stage = (VkShaderStageFlagBits)(1u << i);

				uint32_t mask = pipeline_layout_get_resource_layout(self->current_layout)->spec_constant_mask[i] &
					self->static_state.state.spec_constant_mask;

				if (mask)
				{
					s.pSpecializationInfo = &spec_info[i];
					spec_info[i].pData = self->potential_static_state.spec_constants;
					spec_info[i].dataSize = sizeof(self->potential_static_state.spec_constants);
					spec_info[i].pMapEntries = spec_entries[i];

					FOR_EACH_BIT(mask, bit)
					{
						VkSpecializationMapEntry &entry = spec_entries[i][spec_info[i].mapEntryCount++];
						entry.offset = sizeof(uint32_t) * bit;
						entry.size = sizeof(uint32_t);
						entry.constantID = bit;
					}
				}
			}
		}

		VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipe.layout = self->current_pipeline_layout;
		pipe.renderPass = render_pass_get_render_pass(self->compatible_render_pass);
		pipe.subpass = self->current_subpass;

		pipe.pViewportState = &vp;
		pipe.pDynamicState = &dyn;
		pipe.pColorBlendState = &blend;
		pipe.pDepthStencilState = &ds;
		pipe.pVertexInputState = &vi;
		pipe.pInputAssemblyState = &ia;
		pipe.pMultisampleState = &ms;
		pipe.pRasterizationState = &raster;
		pipe.pStages = stages;
		pipe.stageCount = num_stages;

		VkPipeline pipeline;

		LOGI("Creating graphics pipeline.\n");
		VkResult res = vkCreateGraphicsPipelines(self->device->get_device(), VK_NULL_HANDLE, 1, &pipe, NULL, &pipeline);
		if (res != VK_SUCCESS)
			LOGE("Failed to create graphics pipeline!\n");

		return program_add_pipeline(self->current_program, hash, pipeline);
	}

	void commandbuffer_flush_compute_pipeline(struct CommandBuffer *self)
	{
		Hasher h; hasher_init(&h);
		hasher_u64(&h, self->current_program->intrusive_node.key);

		// Spec constants.
		const CombinedResourceLayout &layout = *pipeline_layout_get_resource_layout(self->current_layout);
		uint32_t combined_spec_constant = layout.combined_spec_constant_mask;
		combined_spec_constant &= self->static_state.state.spec_constant_mask;
		hasher_u32(&h, combined_spec_constant);
		FOR_EACH_BIT(combined_spec_constant, bit)
		{
			hasher_u32(&h, self->potential_static_state.spec_constants[bit]);
		}

		Hash hash = hasher_get(&h);
		self->current_pipeline = program_get_pipeline(self->current_program, hash);
		if (self->current_pipeline == VK_NULL_HANDLE)
			self->current_pipeline = commandbuffer_build_compute_pipeline(self, hash);
	}

	void commandbuffer_flush_graphics_pipeline(struct CommandBuffer *self)
	{
		Hasher h; hasher_init(&h);
		self->active_vbos = 0;
		const CombinedResourceLayout &layout = *pipeline_layout_get_resource_layout(self->current_layout);
		FOR_EACH_BIT(layout.attribute_mask, bit)
		{
			hasher_u32(&h, bit);
			self->active_vbos |= 1u << self->attribs[bit].binding;
			hasher_u32(&h, self->attribs[bit].binding);
			hasher_u32(&h, self->attribs[bit].format);
			hasher_u32(&h, self->attribs[bit].offset);
		}

		FOR_EACH_BIT(self->active_vbos, bit)
		{
			hasher_u32(&h, self->vbo.input_rates[bit]);
			hasher_u32(&h, self->vbo.strides[bit]);
		}

		hasher_u64(&h, self->compatible_render_pass->intrusive_node.key);
		hasher_u32(&h, self->current_subpass);
		hasher_u64(&h, self->current_program->intrusive_node.key);
		hasher_data(&h, self->static_state.words, sizeof(self->static_state.words));

		if (self->static_state.state.blend_enable)
		{
			bool b0 = COMBINER_NEEDS_BLEND_CONSTANT((VkBlendFactor)(self->static_state.state.src_color_blend));
			bool b1 = COMBINER_NEEDS_BLEND_CONSTANT((VkBlendFactor)(self->static_state.state.src_alpha_blend));
			bool b2 = COMBINER_NEEDS_BLEND_CONSTANT((VkBlendFactor)(self->static_state.state.dst_color_blend));
			bool b3 = COMBINER_NEEDS_BLEND_CONSTANT((VkBlendFactor)(self->static_state.state.dst_alpha_blend));
			if (b0 || b1 || b2 || b3)
				hasher_data(&h, (uint32_t *)(self->potential_static_state.blend_constants),
						sizeof(self->potential_static_state.blend_constants));
		}

		// Spec constants.
		uint32_t combined_spec_constant = layout.combined_spec_constant_mask;
		combined_spec_constant &= self->static_state.state.spec_constant_mask;
		hasher_u32(&h, combined_spec_constant);
		FOR_EACH_BIT(combined_spec_constant, bit)
		{
			hasher_u32(&h, self->potential_static_state.spec_constants[bit]);
		}

		Hash hash = hasher_get(&h);
		self->current_pipeline = program_get_pipeline(self->current_program, hash);
		if (self->current_pipeline == VK_NULL_HANDLE)
			self->current_pipeline = commandbuffer_build_graphics_pipeline(self, hash);
	}

	void commandbuffer_flush_compute_state(struct CommandBuffer *self)
	{
		VK_ASSERT(self->current_layout);
		VK_ASSERT(self->current_program);

		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_PIPELINE_BIT))
		{
			VkPipeline old_pipe = self->current_pipeline;
			commandbuffer_flush_compute_pipeline(self);
			if (old_pipe != self->current_pipeline)
				vkCmdBindPipeline(self->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, self->current_pipeline);
		}

		commandbuffer_flush_descriptor_sets(self);

		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
		{
			const VkPushConstantRange &range = pipeline_layout_get_resource_layout(self->current_layout)->push_constant_range;
			if (range.stageFlags != 0)
			{
				VK_ASSERT(range.offset == 0);
				vkCmdPushConstants(self->cmd, self->current_pipeline_layout, range.stageFlags,
						0, range.size,
						self->bindings.push_constant_data);
			}
		}
	}

	void commandbuffer_flush_render_state(struct CommandBuffer *self)
	{
		VK_ASSERT(self->current_layout);
		VK_ASSERT(self->current_program);

		// We've invalidated pipeline state, update the VkPipeline.
		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT |
					COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
		{
			VkPipeline old_pipe = self->current_pipeline;
			commandbuffer_flush_graphics_pipeline(self);
			if (old_pipe != self->current_pipeline)
			{
				vkCmdBindPipeline(self->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, self->current_pipeline);
				commandbuffer_set_dirty(self, COMMAND_BUFFER_DYNAMIC_BITS);
			}
		}

		commandbuffer_flush_descriptor_sets(self);

		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
		{
			const VkPushConstantRange &range = pipeline_layout_get_resource_layout(self->current_layout)->push_constant_range;
			if (range.stageFlags != 0)
			{
				VK_ASSERT(range.offset == 0);
				vkCmdPushConstants(self->cmd, self->current_pipeline_layout, range.stageFlags,
						0, range.size,
						self->bindings.push_constant_data);
			}
		}

		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
			vkCmdSetViewport(self->cmd, 0, 1, &self->viewport);
		if (commandbuffer_get_and_clear(self, COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
			vkCmdSetScissor(self->cmd, 0, 1, &self->scissor);

		uint32_t update_vbo_mask = self->dirty_vbos & self->active_vbos;
		FOR_EACH_BIT_RANGE(update_vbo_mask, binding, binding_count)
		{
#ifdef VULKAN_DEBUG
			for (unsigned i = binding; i < binding + binding_count; i++)
				VK_ASSERT(self->vbo.buffers[i] != VK_NULL_HANDLE);
#endif
			vkCmdBindVertexBuffers(self->cmd, binding, binding_count, self->vbo.buffers + binding, self->vbo.offsets + binding);
		}
		self->dirty_vbos &= ~update_vbo_mask;
	}

	void commandbuffer_set_vertex_attrib(struct CommandBuffer *self, uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
	{
		VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
		VK_ASSERT(self->framebuffer);

		VertexAttribState &attr = self->attribs[attrib];

		if (attr.binding != binding || attr.format != format || attr.offset != offset)
			commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

		VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);

		attr.binding = binding;
		attr.format = format;
		attr.offset = offset;
	}

	void commandbuffer_set_vertex_binding(struct CommandBuffer *self, uint32_t binding, const Buffer &buffer, VkDeviceSize offset, VkDeviceSize stride,
			VkVertexInputRate step_rate)
	{
		VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
		VK_ASSERT(self->framebuffer);

		VkBuffer vkbuffer = buffer_get_buffer(&buffer);
		if (self->vbo.buffers[binding] != vkbuffer || self->vbo.offsets[binding] != offset)
			self->dirty_vbos |= 1u << binding;
		if (self->vbo.strides[binding] != stride || self->vbo.input_rates[binding] != step_rate)
			commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT);

		self->vbo.buffers[binding] = vkbuffer;
		self->vbo.offsets[binding] = offset;
		self->vbo.strides[binding] = stride;
		self->vbo.input_rates[binding] = step_rate;
	}

	void commandbuffer_set_scissor(struct CommandBuffer *self, const VkRect2D &rect)
	{
		VK_ASSERT(self->framebuffer);
		VK_ASSERT(rect.offset.x >= 0);
		VK_ASSERT(rect.offset.y >= 0);
		self->scissor = rect;
		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
	}

	void commandbuffer_push_constants(struct CommandBuffer *self, const void *data, VkDeviceSize offset, VkDeviceSize range)
	{
		VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
		memcpy(self->bindings.push_constant_data + offset, data, range);
		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
	}


	void commandbuffer_set_program(struct CommandBuffer *self, Program &program)
	{
		if (self->current_program == &program)
			return;

		self->current_program = &program;
		self->current_pipeline = VK_NULL_HANDLE;

		VK_ASSERT((self->framebuffer && program_get_shader(self->current_program, ShaderStage_Vertex)) ||
				(!self->framebuffer && program_get_shader(self->current_program, ShaderStage_Compute)));

		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);

		if (!self->current_layout)
		{
			self->dirty_sets = ~0u;
			commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);

			self->current_layout = program_get_pipeline_layout(&program);
			self->current_pipeline_layout = pipeline_layout_get_layout(self->current_layout);
		}
		else if (program_get_pipeline_layout(&program)->intrusive_node.key != self->current_layout->intrusive_node.key)
		{
			const CombinedResourceLayout &new_layout = *pipeline_layout_get_resource_layout(program_get_pipeline_layout(&program));
			const CombinedResourceLayout &old_layout = *pipeline_layout_get_resource_layout(self->current_layout);

			// If the push constant layout changes, all descriptor sets
			// are invalidated.
			if (new_layout.push_constant_layout_hash != old_layout.push_constant_layout_hash)
			{
				self->dirty_sets = ~0u;
				commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
			}
			else
			{
				// Find the first set whose descriptor set layout differs.
				PipelineLayout *new_pipe_layout = program_get_pipeline_layout(&program);
				for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
				{
					if (pipeline_layout_get_allocator(new_pipe_layout, set) != pipeline_layout_get_allocator(self->current_layout, set))
					{
						self->dirty_sets |= ~((1u << set) - 1);
						break;
					}
				}
			}
			self->current_layout = program_get_pipeline_layout(&program);
			self->current_pipeline_layout = pipeline_layout_get_layout(self->current_layout);
		}
	}

	void *commandbuffer_allocate_constant_data(struct CommandBuffer *self, unsigned set, unsigned binding, VkDeviceSize size)
	{
		BufferBlockAllocation data = bufferblock_allocate(&self->ubo_block, size);
		if (!data.host)
		{
			self->device->request_uniform_block(self->ubo_block, size);
			data = bufferblock_allocate(&self->ubo_block, size);
		}
		commandbuffer_set_uniform_buffer(self, set, binding, *bh_get(&self->ubo_block.gpu), data.offset, size);
		return data.host;
	}

	void *commandbuffer_allocate_vertex_data(struct CommandBuffer *self, unsigned binding, VkDeviceSize size, VkDeviceSize stride,
			VkVertexInputRate step_rate)
	{
		BufferBlockAllocation data = bufferblock_allocate(&self->vbo_block, size);
		if (!data.host)
		{
			self->device->request_vertex_block(self->vbo_block, size);
			data = bufferblock_allocate(&self->vbo_block, size);
		}

		commandbuffer_set_vertex_binding(self, binding, *bh_get(&self->vbo_block.gpu), data.offset, stride, step_rate);
		return data.host;
	}

	void commandbuffer_set_uniform_buffer(struct CommandBuffer *self, unsigned set, unsigned binding, const Buffer &buffer, VkDeviceSize offset,
			VkDeviceSize range)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(buffer_get_create_info(&buffer).usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		ResourceBinding &b = self->bindings.bindings[set][binding];

		if (buffer.cookie_base.cookie == self->bindings.cookies[set][binding] && b.buffer.offset == offset && b.buffer.range == range)
			return;

		b.buffer = { buffer_get_buffer(&buffer), offset, range };
		self->bindings.cookies[set][binding] = buffer.cookie_base.cookie;
		self->bindings.secondary_cookies[set][binding] = 0;
		self->dirty_sets |= 1u << set;
	}

	void commandbuffer_set_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const Sampler &sampler)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		if (sampler.cookie_base.cookie == self->bindings.secondary_cookies[set][binding])
			return;

		ResourceBinding &b = self->bindings.bindings[set][binding];
		b.image.fp.sampler = sampler_get_sampler(&sampler);
		b.image.integer.sampler = sampler_get_sampler(&sampler);
		self->dirty_sets |= 1u << set;
		self->bindings.secondary_cookies[set][binding] = sampler.cookie_base.cookie;
	}

	void commandbuffer_set_buffer_view(struct CommandBuffer *self, unsigned set, unsigned binding, const BufferView &view)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(buffer_get_create_info(&bufferview_get_buffer(&view)).usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
		if (view.cookie_base.cookie == self->bindings.cookies[set][binding])
			return;
		ResourceBinding &b = self->bindings.bindings[set][binding];
		b.buffer_view = bufferview_get_view(&view);
		self->bindings.cookies[set][binding] = view.cookie_base.cookie;
		self->bindings.secondary_cookies[set][binding] = 0;
		self->dirty_sets |= 1u << set;
	}

	void commandbuffer_set_input_attachments(struct CommandBuffer *self, unsigned set, unsigned start_binding)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(start_binding + render_pass_get_num_input_attachments(self->actual_render_pass, self->current_subpass) <= VULKAN_NUM_BINDINGS);
		unsigned num_input_attachments = render_pass_get_num_input_attachments(self->actual_render_pass, self->current_subpass);
		for (unsigned i = 0; i < num_input_attachments; i++)
		{
			const VkAttachmentReference &ref = *render_pass_get_input_attachment(self->actual_render_pass, self->current_subpass, i);
			if (ref.attachment == VK_ATTACHMENT_UNUSED)
				continue;

			ImageView *view = framebuffer_get_attachment(self->framebuffer, ref.attachment);
			VK_ASSERT(view);
			VK_ASSERT(image_get_create_info(&imageview_get_image(view)).usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			if (view->cookie_base.cookie == self->bindings.cookies[set][start_binding + i] &&
					self->bindings.bindings[set][start_binding + i].image.fp.imageLayout == ref.layout)
			{
				continue;
			}

			ResourceBinding &b = self->bindings.bindings[set][start_binding + i];
			b.image.fp.imageLayout = ref.layout;
			b.image.integer.imageLayout = ref.layout;
			b.image.fp.imageView = imageview_get_float_view(view);
			b.image.integer.imageView = imageview_get_integer_view(view);
			self->bindings.cookies[set][start_binding + i] = view->cookie_base.cookie;
			self->dirty_sets |= 1u << set;
		}
	}

	void commandbuffer_set_texture_raw(struct CommandBuffer *self, unsigned set, unsigned binding,
			VkImageView float_view, VkImageView integer_view,
			VkImageLayout layout,
			uint64_t cookie)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);

		if (cookie == self->bindings.cookies[set][binding] && self->bindings.bindings[set][binding].image.fp.imageLayout == layout)
			return;

		ResourceBinding &b = self->bindings.bindings[set][binding];
		b.image.fp.imageLayout = layout;
		b.image.fp.imageView = float_view;
		b.image.integer.imageLayout = layout;
		b.image.integer.imageView = integer_view;
		self->bindings.cookies[set][binding] = cookie;
		self->dirty_sets |= 1u << set;
	}

	void commandbuffer_set_texture_view(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view)
	{
		VK_ASSERT(image_get_create_info(&imageview_get_image_const(&view)).usage & VK_IMAGE_USAGE_SAMPLED_BIT);
		commandbuffer_set_texture_raw(self, set, binding, imageview_get_float_view(&view), imageview_get_integer_view(&view),
				image_get_layout(&imageview_get_image_const(&view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), view.cookie_base.cookie);
	}

	void commandbuffer_set_texture_view_sampler(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, const Sampler &sampler)
	{
		commandbuffer_set_sampler(self, set, binding, sampler);
		commandbuffer_set_texture_view(self, set, binding, view);
	}

	void commandbuffer_set_texture_view_stock(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view, StockSampler stock)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(image_get_create_info(&imageview_get_image_const(&view)).usage & VK_IMAGE_USAGE_SAMPLED_BIT);
		const Sampler &sampler = self->device->get_stock_sampler(stock);
		commandbuffer_set_texture_view_sampler(self, set, binding, view, sampler);
	}

	void commandbuffer_set_storage_texture(struct CommandBuffer *self, unsigned set, unsigned binding, const ImageView &view)
	{
		VK_ASSERT(image_get_create_info(&imageview_get_image_const(&view)).usage & VK_IMAGE_USAGE_STORAGE_BIT);
		commandbuffer_set_texture_raw(self, set, binding, imageview_get_float_view(&view), imageview_get_integer_view(&view),
				image_get_layout(&imageview_get_image_const(&view), VK_IMAGE_LAYOUT_GENERAL), view.cookie_base.cookie);
	}

	void commandbuffer_flush_descriptor_set(struct CommandBuffer *self, uint32_t set)
	{
		const CombinedResourceLayout &layout = *pipeline_layout_get_resource_layout(self->current_layout);
		const DescriptorSetLayout &set_layout = layout.sets[set];
		uint32_t num_dynamic_offsets = 0;
		uint32_t dynamic_offsets[VULKAN_NUM_BINDINGS];
		Hasher h; hasher_init(&h);

		hasher_u32(&h, set_layout.fp_mask);

		// UBOs
		FOR_EACH_BIT(set_layout.uniform_buffer_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			hasher_u32(&h, self->bindings.bindings[set][binding].buffer.range);
			VK_ASSERT(self->bindings.bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);

			dynamic_offsets[num_dynamic_offsets++] = self->bindings.bindings[set][binding].buffer.offset;
		}

		// SSBOs
		FOR_EACH_BIT(set_layout.storage_buffer_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			hasher_u32(&h, self->bindings.bindings[set][binding].buffer.offset);
			hasher_u32(&h, self->bindings.bindings[set][binding].buffer.range);
			VK_ASSERT(self->bindings.bindings[set][binding].buffer.buffer != VK_NULL_HANDLE);
		}

		// Sampled buffers
		FOR_EACH_BIT(set_layout.sampled_buffer_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			VK_ASSERT(self->bindings.bindings[set][binding].buffer_view != VK_NULL_HANDLE);
		}

		// Sampled images
		FOR_EACH_BIT(set_layout.sampled_image_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			if (!has_immutable_sampler(set_layout, binding))
			{
				hasher_u64(&h, self->bindings.secondary_cookies[set][binding]);
				VK_ASSERT(self->bindings.bindings[set][binding].image.fp.sampler != VK_NULL_HANDLE);
			}
			hasher_u32(&h, self->bindings.bindings[set][binding].image.fp.imageLayout);
			VK_ASSERT(self->bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

		// Separate images
		FOR_EACH_BIT(set_layout.separate_image_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			hasher_u32(&h, self->bindings.bindings[set][binding].image.fp.imageLayout);
			VK_ASSERT(self->bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

		// Separate samplers
		FOR_EACH_BIT(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, binding)
		{
			hasher_u64(&h, self->bindings.secondary_cookies[set][binding]);
			VK_ASSERT(self->bindings.bindings[set][binding].image.fp.sampler != VK_NULL_HANDLE);
		}

		// Storage images
		FOR_EACH_BIT(set_layout.storage_image_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			hasher_u32(&h, self->bindings.bindings[set][binding].image.fp.imageLayout);
			VK_ASSERT(self->bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

		// Input attachments
		FOR_EACH_BIT(set_layout.input_attachment_mask, binding)
		{
			hasher_u64(&h, self->bindings.cookies[set][binding]);
			hasher_u32(&h, self->bindings.bindings[set][binding].image.fp.imageLayout);
			VK_ASSERT(self->bindings.bindings[set][binding].image.fp.imageView != VK_NULL_HANDLE);
		}

		Hash hash = hasher_get(&h);
		DescriptorSetAllocation allocated = descriptor_set_allocator_find(pipeline_layout_get_allocator(self->current_layout, set), hash);

		// The descriptor set was not successfully cached, rebuild.
		if (!allocated.cached)
		{
			uint32_t write_count = 0;
			uint32_t buffer_info_count = 0;
			VkWriteDescriptorSet writes[VULKAN_NUM_BINDINGS];
			VkDescriptorBufferInfo buffer_info[VULKAN_NUM_BINDINGS];

			FOR_EACH_BIT(set_layout.uniform_buffer_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;

				// Offsets are applied dynamically.
				VkDescriptorBufferInfo &buffer = buffer_info[buffer_info_count++];
				buffer = self->bindings.bindings[set][binding].buffer;
				buffer.offset = 0;
				write.pBufferInfo = &buffer;
			}

			FOR_EACH_BIT(set_layout.storage_buffer_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;
				write.pBufferInfo = &self->bindings.bindings[set][binding].buffer;
			}

			FOR_EACH_BIT(set_layout.sampled_buffer_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;
				write.pTexelBufferView = &self->bindings.bindings[set][binding].buffer_view;
			}

			FOR_EACH_BIT(set_layout.sampled_image_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;

				if (set_layout.fp_mask & (1u << binding))
					write.pImageInfo = &self->bindings.bindings[set][binding].image.fp;
				else
					write.pImageInfo = &self->bindings.bindings[set][binding].image.integer;
			}

			FOR_EACH_BIT(set_layout.separate_image_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;

				if (set_layout.fp_mask & (1u << binding))
					write.pImageInfo = &self->bindings.bindings[set][binding].image.fp;
				else
					write.pImageInfo = &self->bindings.bindings[set][binding].image.integer;
			}

			FOR_EACH_BIT(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;
				write.pImageInfo = &self->bindings.bindings[set][binding].image.fp;
			}

			FOR_EACH_BIT(set_layout.storage_image_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;

				if (set_layout.fp_mask & (1u << binding))
					write.pImageInfo = &self->bindings.bindings[set][binding].image.fp;
				else
					write.pImageInfo = &self->bindings.bindings[set][binding].image.integer;
			}

			FOR_EACH_BIT(set_layout.input_attachment_mask, binding)
			{
				VkWriteDescriptorSet &write = writes[write_count++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = NULL;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				write.dstArrayElement = 0;
				write.dstBinding = binding;
				write.dstSet = allocated.set;
				if (set_layout.fp_mask & (1u << binding))
					write.pImageInfo = &self->bindings.bindings[set][binding].image.fp;
				else
					write.pImageInfo = &self->bindings.bindings[set][binding].image.integer;
			}

			vkUpdateDescriptorSets(self->device->get_device(), write_count, writes, 0, NULL);
		}

		vkCmdBindDescriptorSets(self->cmd, self->actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
				self->current_pipeline_layout, set, 1, &allocated.set, num_dynamic_offsets, dynamic_offsets);
	}

	void commandbuffer_flush_descriptor_sets(struct CommandBuffer *self)
	{
		const CombinedResourceLayout &layout = *pipeline_layout_get_resource_layout(self->current_layout);
		uint32_t set_update = layout.descriptor_set_mask & self->dirty_sets;
		FOR_EACH_BIT(set_update, set)
		{ commandbuffer_flush_descriptor_set(self, set); 	}
		self->dirty_sets &= ~set_update;
	}

	void commandbuffer_draw(struct CommandBuffer *self, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
	{
		VK_ASSERT(self->current_program);
		VK_ASSERT(!self->is_compute);
		commandbuffer_flush_render_state(self);
		vkCmdDraw(self->cmd, vertex_count, instance_count, first_vertex, first_instance);
	}

	void commandbuffer_dispatch(struct CommandBuffer *self, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
	{
		VK_ASSERT(self->current_program);
		VK_ASSERT(self->is_compute);
		commandbuffer_flush_compute_state(self);
		vkCmdDispatch(self->cmd, groups_x, groups_y, groups_z);
	}

	void commandbuffer_set_opaque_state(struct CommandBuffer *self)
	{
		PipelineState::State &state = self->static_state.state;
		memset(&state, 0, sizeof(state));
		state.cull_mode = VK_CULL_MODE_BACK_BIT;
		state.blend_enable = false;
		state.depth_test = true;
		state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
		state.depth_write = true;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}

	void commandbuffer_set_quad_state(struct CommandBuffer *self)
	{
		PipelineState::State &state = self->static_state.state;
		memset(&state, 0, sizeof(state));
		state.cull_mode = VK_CULL_MODE_NONE;
		state.blend_enable = false;
		state.depth_test = false;
		state.depth_write = false;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		commandbuffer_set_dirty(self, COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}

	void commandbuffer_end(struct CommandBuffer *self)
	{
		if (vkEndCommandBuffer(self->cmd) != VK_SUCCESS)
			LOGE("Failed to end command buffer.\n");

		if (self->vbo_block.mapped)
			self->device->request_vertex_block_nolock(self->vbo_block, 0);
		if (self->ubo_block.mapped)
			self->device->request_uniform_block_nolock(self->ubo_block, 0);
	}

	void commandbuffer_begin_region(struct CommandBuffer *self, const char *name, const float *color)
	{
		if (self->device->ext.supports_debug_marker)
		{
			VkDebugMarkerMarkerInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT };
			if (color)
			{
				for (unsigned i = 0; i < 4; i++)
					info.color[i] = color[i];
			}
			else
			{
				for (unsigned i = 0; i < 4; i++)
					info.color[i] = 1.0f;
			}

			info.pMarkerName = name;
			vkCmdDebugMarkerBeginEXT(self->cmd, &info);
		}
	}

	void commandbuffer_end_region(struct CommandBuffer *self)
	{
		if (self->device->ext.supports_debug_marker)
			vkCmdDebugMarkerEndEXT(self->cmd);
	}


	void CommandBufferDeleter::operator()(CommandBuffer *cmd)
	{
		{ struct ObjectPoolRaw *_pool = &cmd->device->handle_pool.command_buffers; commandbuffer_fini(cmd); object_pool_raw_free(_pool, cmd); }
	}

/* === vulkan.cpp === */


#ifndef _WIN32
#include <dlfcn.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

//#undef VULKAN_DEBUG


	static bool has_vk_extension(const VkExtensionProperties *exts, uint32_t count, const char *name)
	{
		uint32_t i;
		for (i = 0; i < count; i++)
			if (strcmp(exts[i].extensionName, name) == 0)
				return true;
		return false;
	}

	static bool has_vk_layer(const VkLayerProperties *layers, uint32_t count, const char *name)
	{
		uint32_t i;
		for (i = 0; i < count; i++)
			if (strcmp(layers[i].layerName, name) == 0)
				return true;
		return false;
	}

	bool context_init_loader(PFN_vkGetInstanceProcAddr addr)
	{
		if (!addr)
		{
#ifndef _WIN32
			static void *module;
			if (!module)
			{
				const char *vulkan_path = getenv("GRANITE_VULKAN_LIBRARY");
				if (vulkan_path)
					module = dlopen(vulkan_path, RTLD_LOCAL | RTLD_LAZY);
				if (!module)
					module = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_LAZY);
				if (!module)
					module = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_LAZY);
				if (!module)
					return false;
			}

			addr = (PFN_vkGetInstanceProcAddr)(dlsym(module, "vkGetInstanceProcAddr"));
			if (!addr)
				return false;
#else
			static HMODULE module;
			if (!module)
			{
				module = LoadLibraryA("vulkan-1.dll");
				if (!module)
					return false;
			}

			addr = (PFN_vkGetInstanceProcAddr)(GetProcAddress(module, "vkGetInstanceProcAddr"));
			if (!addr)
				return false;
#endif
		}

		vkGetInstanceProcAddr = addr;
		volkGenLoadLoader(NULL, vkGetInstanceProcAddrStub);
		return true;
	}

	bool context_init(Context *ctx, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
			const char **required_device_extensions, unsigned num_required_device_extensions,
			const char **required_device_layers, unsigned num_required_device_layers,
			const VkPhysicalDeviceFeatures *required_features)
	{
		/* Replicate the former default member initialisers explicitly (a malloc'd
		 * Context has indeterminate members otherwise). gpu_props/mem_props/ext are
		 * outputs of create_device, written before any read and guarded by
		 * is_valid(); zero them defensively. */
		ctx->device = VK_NULL_HANDLE;
		ctx->instance = instance;
		ctx->gpu = VK_NULL_HANDLE;
		ctx->graphics_queue = VK_NULL_HANDLE;
		ctx->compute_queue = VK_NULL_HANDLE;
		ctx->transfer_queue = VK_NULL_HANDLE;
		ctx->graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
		ctx->compute_queue_family = VK_QUEUE_FAMILY_IGNORED;
		ctx->transfer_queue_family = VK_QUEUE_FAMILY_IGNORED;
		ctx->owned_device = true;
		ctx->valid = false;
		memset(&ctx->gpu_props, 0, sizeof(ctx->gpu_props));
		memset(&ctx->mem_props, 0, sizeof(ctx->mem_props));
		memset(&ctx->ext, 0, sizeof(ctx->ext));

		/* Load global+instance function pointers using application-created VkInstance. */
		volkGenLoadInstance(instance, vkGetInstanceProcAddrStub);
		volkGenLoadDevice(instance, vkGetInstanceProcAddrStub);
		if (!context_create_device(ctx, gpu, surface, required_device_extensions, num_required_device_extensions, required_device_layers,
					num_required_device_layers, required_features))
		{
			LOGE("Failed to create Vulkan device.\n");
			context_destroy(ctx);
			return false;
		}
		ctx->valid = true;
		return true;
	}

	void context_deinit(Context *ctx)
	{
		context_destroy(ctx);
	}

	void context_destroy(struct Context *self)
	{
		if (self->device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(self->device);

		if (self->owned_device && self->device != VK_NULL_HANDLE)
			vkDestroyDevice(self->device, NULL);
	}

	bool context_create_device(struct Context *self, VkPhysicalDevice gpu, VkSurfaceKHR surface, const char **required_device_extensions,
			unsigned num_required_device_extensions, const char **required_device_layers,
			unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features)
	{
		if (gpu == VK_NULL_HANDLE)
		{
			uint32_t gpu_count = 0;
			if (vkEnumeratePhysicalDevices(self->instance, &gpu_count, NULL) != VK_SUCCESS)
			{
				LOGE("vkEnumeratePhysicalDevices failed.\n");
				return false;
			}

			if (gpu_count == 0)
				return false;

			VkPhysicalDevice *gpus = (VkPhysicalDevice *)malloc(gpu_count * sizeof(VkPhysicalDevice));
			if (vkEnumeratePhysicalDevices(self->instance, &gpu_count, gpus) != VK_SUCCESS)
			{
				LOGE("vkEnumeratePhysicalDevices failed.\n");
				free(gpus);
				return false;
			}

			for (uint32_t gi = 0; gi < gpu_count; gi++)
			{
				VkPhysicalDevice cur = gpus[gi];
				VkPhysicalDeviceProperties props;
				vkGetPhysicalDeviceProperties(cur, &props);
				LOGI("Found Vulkan GPU: %s\n", props.deviceName);
				LOGI("    API: %u.%u.%u\n",
						VK_VERSION_MAJOR(props.apiVersion),
						VK_VERSION_MINOR(props.apiVersion),
						VK_VERSION_PATCH(props.apiVersion));
				LOGI("    Driver: %u.%u.%u\n",
						VK_VERSION_MAJOR(props.driverVersion),
						VK_VERSION_MINOR(props.driverVersion),
						VK_VERSION_PATCH(props.driverVersion));
			}

			const char *gpu_index = getenv("GRANITE_VULKAN_DEVICE_INDEX");
			if (gpu_index)
			{
				unsigned index = strtoul(gpu_index, NULL, 0);
				if (index < gpu_count)
					gpu = gpus[index];
			}

			if (gpu == VK_NULL_HANDLE)
				gpu = gpus[0];
			free(gpus);
		}

		uint32_t ext_count = 0;
		vkEnumerateDeviceExtensionProperties(gpu, NULL, &ext_count, NULL);
		VkExtensionProperties *queried_extensions = (VkExtensionProperties *)malloc((ext_count ? ext_count : 1) * sizeof(VkExtensionProperties));
		if (ext_count)
			vkEnumerateDeviceExtensionProperties(gpu, NULL, &ext_count, queried_extensions);

		uint32_t layer_count = 0;
		vkEnumerateDeviceLayerProperties(gpu, &layer_count, NULL);
		VkLayerProperties *queried_layers = (VkLayerProperties *)malloc((layer_count ? layer_count : 1) * sizeof(VkLayerProperties));
		if (layer_count)
			vkEnumerateDeviceLayerProperties(gpu, &layer_count, queried_layers);

		for (uint32_t i = 0; i < num_required_device_extensions; i++)
			if (!has_vk_extension(queried_extensions, ext_count, required_device_extensions[i]))
			{ free(queried_extensions); free(queried_layers); return false; }

		for (uint32_t i = 0; i < num_required_device_layers; i++)
			if (!has_vk_layer(queried_layers, layer_count, required_device_layers[i]))
			{ free(queried_extensions); free(queried_layers); return false; }

		self->gpu = gpu;
		vkGetPhysicalDeviceProperties(gpu, &self->gpu_props);
		vkGetPhysicalDeviceMemoryProperties(gpu, &self->mem_props);

		LOGI("Selected Vulkan GPU: %s\n", self->gpu_props.deviceName);

		if (self->gpu_props.apiVersion >= VK_API_VERSION_1_1)
			LOGI("GPU supports Vulkan 1.1.\n");
		else if (self->gpu_props.apiVersion >= VK_API_VERSION_1_0)
			LOGI("GPU supports Vulkan 1.0.\n");

		uint32_t queue_count;
		vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, NULL);
		VkQueueFamilyProperties *queue_props = (VkQueueFamilyProperties *)malloc((queue_count ? queue_count : 1) * sizeof(VkQueueFamilyProperties));
		vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_count, queue_props);

		for (unsigned i = 0; i < queue_count; i++)
		{
			VkBool32 supported = surface == VK_NULL_HANDLE;
			if (surface != VK_NULL_HANDLE)
				vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &supported);

			static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT;
			if (supported && ((queue_props[i].queueFlags & required) == required))
			{
				self->graphics_queue_family = i;
				break;
			}
		}

		for (unsigned i = 0; i < queue_count; i++)
		{
			static const VkQueueFlags required = VK_QUEUE_COMPUTE_BIT;
			if (i != self->graphics_queue_family && (queue_props[i].queueFlags & required) == required)
			{
				self->compute_queue_family = i;
				break;
			}
		}

		for (unsigned i = 0; i < queue_count; i++)
		{
			static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
			if (i != self->graphics_queue_family && i != self->compute_queue_family && (queue_props[i].queueFlags & required) == required)
			{
				self->transfer_queue_family = i;
				break;
			}
		}

		if (self->transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
		{
			for (unsigned i = 0; i < queue_count; i++)
			{
				static const VkQueueFlags required = VK_QUEUE_TRANSFER_BIT;
				if (i != self->graphics_queue_family && (queue_props[i].queueFlags & required) == required)
				{
					self->transfer_queue_family = i;
					break;
				}
			}
		}

		if (self->graphics_queue_family == VK_QUEUE_FAMILY_IGNORED)
		{ free(queried_extensions); free(queried_layers); free(queue_props); return false; }

		unsigned universal_queue_index = 1;
		uint32_t graphics_queue_index = 0;
		uint32_t compute_queue_index = 0;
		uint32_t transfer_queue_index = 0;

		if (self->compute_queue_family == VK_QUEUE_FAMILY_IGNORED)
		{
			self->compute_queue_family = self->graphics_queue_family;
			compute_queue_index = min_(queue_props[self->graphics_queue_family].queueCount - 1, universal_queue_index);
			universal_queue_index++;
		}

		if (self->transfer_queue_family == VK_QUEUE_FAMILY_IGNORED)
		{
			self->transfer_queue_family = self->graphics_queue_family;
			transfer_queue_index = min_(queue_props[self->graphics_queue_family].queueCount - 1, universal_queue_index);
			universal_queue_index++;
		}
		else if (self->transfer_queue_family == self->compute_queue_family)
			transfer_queue_index = min_(queue_props[self->compute_queue_family].queueCount - 1, 1u);

		static const float graphics_queue_prio = 0.5f;
		static const float compute_queue_prio = 1.0f;
		static const float transfer_queue_prio = 1.0f;
		float prio[3] = { graphics_queue_prio, compute_queue_prio, transfer_queue_prio };

		unsigned queue_family_count = 0;
		VkDeviceQueueCreateInfo queue_info[3] = {};

		VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		device_info.pQueueCreateInfos = queue_info;

		queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info[queue_family_count].queueFamilyIndex = self->graphics_queue_family;
		queue_info[queue_family_count].queueCount = min_(universal_queue_index,
				queue_props[self->graphics_queue_family].queueCount);
		queue_info[queue_family_count].pQueuePriorities = prio;
		queue_family_count++;

		if (self->compute_queue_family != self->graphics_queue_family)
		{
			queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_info[queue_family_count].queueFamilyIndex = self->compute_queue_family;
			queue_info[queue_family_count].queueCount = min_(self->transfer_queue_family == self->compute_queue_family ? 2u : 1u,
					queue_props[self->compute_queue_family].queueCount);
			queue_info[queue_family_count].pQueuePriorities = prio + 1;
			queue_family_count++;
		}

		if (self->transfer_queue_family != self->graphics_queue_family && self->transfer_queue_family != self->compute_queue_family)
		{
			queue_info[queue_family_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_info[queue_family_count].queueFamilyIndex = self->transfer_queue_family;
			queue_info[queue_family_count].queueCount = 1;
			queue_info[queue_family_count].pQueuePriorities = prio + 2;
			queue_family_count++;
		}

		device_info.queueCreateInfoCount = queue_family_count;

		/* At most the caller-required extensions plus a fixed set of optional ones
		 * added below; size to that bound exactly. */
		const char **enabled_extensions = (const char **)malloc((num_required_device_extensions + 16) * sizeof(const char *));
		unsigned enabled_extensions_count = 0;
		const char **enabled_layers = (const char **)malloc((num_required_device_layers + 4) * sizeof(const char *));
		unsigned enabled_layers_count = 0;

		for (uint32_t i = 0; i < num_required_device_extensions; i++)
			enabled_extensions[enabled_extensions_count++] = (required_device_extensions[i]);
		for (uint32_t i = 0; i < num_required_device_layers; i++)
			enabled_layers[enabled_layers_count++] = (required_device_layers[i]);

		if (has_vk_extension(queried_extensions, ext_count, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) &&
				has_vk_extension(queried_extensions, ext_count, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
		{
			self->ext.supports_dedicated = true;
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
		}

		if (has_vk_extension(queried_extensions, ext_count, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
		{
			self->ext.supports_debug_marker = true;
			enabled_extensions[enabled_extensions_count++] = (VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
		}

#ifdef _WIN32
		self->ext.supports_external = false;
#else
		if (self->ext.supports_external && self->ext.supports_dedicated &&
				has_vk_extension(queried_extensions, ext_count, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME) &&
				has_vk_extension(queried_extensions, ext_count, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) &&
				has_vk_extension(queried_extensions, ext_count, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME) &&
				has_vk_extension(queried_extensions, ext_count, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME))
		{
			self->ext.supports_external = true;
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
		}
		else
			self->ext.supports_external = false;
#endif

		VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };

		if (has_vk_extension(queried_extensions, ext_count, VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME))
			enabled_extensions[enabled_extensions_count++] = (VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);

		vkGetPhysicalDeviceFeatures(gpu, &features.features);

		// Enable device features we might care about.
		{
			VkPhysicalDeviceFeatures enabled_features = *required_features;
			if (features.features.textureCompressionETC2)
				enabled_features.textureCompressionETC2 = VK_TRUE;
			if (features.features.textureCompressionBC)
				enabled_features.textureCompressionBC = VK_TRUE;
			if (features.features.textureCompressionASTC_LDR)
				enabled_features.textureCompressionASTC_LDR = VK_TRUE;
			if (features.features.fullDrawIndexUint32)
				enabled_features.fullDrawIndexUint32 = VK_TRUE;
			if (features.features.imageCubeArray)
				enabled_features.imageCubeArray = VK_TRUE;
			if (features.features.fillModeNonSolid)
				enabled_features.fillModeNonSolid = VK_TRUE;
			if (features.features.independentBlend)
				enabled_features.independentBlend = VK_TRUE;
			if (features.features.sampleRateShading)
				enabled_features.sampleRateShading = VK_TRUE;
			if (features.features.fragmentStoresAndAtomics)
				enabled_features.fragmentStoresAndAtomics = VK_TRUE;
			if (features.features.shaderStorageImageExtendedFormats)
				enabled_features.shaderStorageImageExtendedFormats = VK_TRUE;
			if (features.features.shaderStorageImageMultisample)
				enabled_features.shaderStorageImageMultisample = VK_TRUE;
			if (features.features.largePoints)
				enabled_features.largePoints = VK_TRUE;

			features.features = enabled_features;
			self->ext.enabled_features = enabled_features;
		}

		device_info.pEnabledFeatures = &features.features;

#ifdef VULKAN_DEBUG
		{
			bool force_no_validation = false;
			const char *no_validation = getenv("GRANITE_VULKAN_NO_VALIDATION");
			if (no_validation && strtoul(no_validation, NULL, 0) != 0)
				force_no_validation = true;
			if (!force_no_validation && has_vk_layer(queried_layers, layer_count, "VK_LAYER_LUNARG_standard_validation"))
				enabled_layers[enabled_layers_count++] = ("VK_LAYER_LUNARG_standard_validation");
		}
#endif

		device_info.enabledExtensionCount = enabled_extensions_count;
		device_info.ppEnabledExtensionNames = enabled_extensions_count ? enabled_extensions : NULL;
		device_info.enabledLayerCount = enabled_layers_count;
		device_info.ppEnabledLayerNames = enabled_layers_count ? enabled_layers : NULL;

		free(queried_extensions);
		free(queried_layers);
		free(queue_props);

		VkResult dev_res = vkCreateDevice(gpu, &device_info, NULL, &self->device);
		free(enabled_extensions);
		free(enabled_layers);
		if (dev_res != VK_SUCCESS)
			return false;

		/* Load global function pointers using application-created VkDevice. */
		volkGenLoadDevice(self->device, vkGetDeviceProcAddrStub);
		vkGetDeviceQueue(self->device, self->graphics_queue_family, graphics_queue_index, &self->graphics_queue);
		vkGetDeviceQueue(self->device, self->compute_queue_family, compute_queue_index, &self->compute_queue);
		vkGetDeviceQueue(self->device, self->transfer_queue_family, transfer_queue_index, &self->transfer_queue);

		return true;
	}


	/* Establish a Device in raw (uninitialised) storage. This sets every member
	 * that the members' constructors and NSDMIs would, so it is correct when called
	 * on malloc'd memory where no constructor has run. No member allocates at this
	 * point, so the established state is simply "empty". */
	void Device::device_init(Device *self)
	{
		/* Scalar handles / ids (the VK_NULL_HANDLE and 0 NSDMIs). */
		self->instance       = VK_NULL_HANDLE;
		self->gpu            = VK_NULL_HANDLE;
		self->device         = VK_NULL_HANDLE;
		self->graphics_queue = VK_NULL_HANDLE;
		self->compute_queue  = VK_NULL_HANDLE;
		self->transfer_queue = VK_NULL_HANDLE;
		self->cookie         = 0;
		self->frame_context_index         = 0;
		self->graphics_queue_family_index = 0;
		self->compute_queue_family_index  = 0;
		self->transfer_queue_family_index = 0;
		self->lock.counter   = 0;

		/* POD aggregates with no NSDMI - the constructor leaves them indeterminate
		 * and set_context()/init() fill them before use; zero them so a malloc'd
		 * Device starts from a defined state. */
		memset(&self->mem_props,   0, sizeof(self->mem_props));
		memset(&self->gpu_props,   0, sizeof(self->gpu_props));
		memset(&self->ext,         0, sizeof(self->ext));
		memset(&self->workarounds, 0, sizeof(self->workarounds));

		/* Fixed stock-sampler handle array: each SamplerHandle is a single pointer
		 * defaulting to NULL, so a zero-fill is the empty state. */
		memset(self->samplers, 0, sizeof(self->samplers));

		/* Per-queue submission state. Each QueueData holds a SemaphoreHandleVec and
		 * a VkPipelineStageVec (both { items=NULL, count=0, cap=0 } when empty) plus
		 * a need_fence flag; none has its constructor run under malloc, so zero them
		 * to their empty state. clear_wait_semaphores() iterates these during the
		 * very first set_context()/init_frame_contexts(), so they must be valid here. */
		memset(&self->graphics, 0, sizeof(self->graphics));
		memset(&self->compute,  0, sizeof(self->compute));
		memset(&self->transfer, 0, sizeof(self->transfer));

		/* Pending CPU->GPU DMA staging lists (BufferBlockVec vbo/ubo): ctor-only,
		 * empty state is { NULL, 0, 0 }; zero so the first submit/end-frame that
		 * iterates or clears them is valid. */
		memset(&self->dma, 0, sizeof(self->dma));

		/* Owning members - establish each one's empty state via its raw-memory init. */
		self->handle_pool.init();
		deviceallocator_init_empty(&self->managers.memory);
		fencemanager_init_empty(&self->managers.fence);
		semaphoremanager_init_empty(&self->managers.semaphore);
		bufferpool_init_empty(&self->managers.vbo);
		bufferpool_init_empty(&self->managers.ubo);
		per_frame_ptr_vec_init_empty(&self->per_frame);
		pipeline_layout_map_init(&self->pipeline_layouts);
		descriptor_set_allocator_map_init(&self->descriptor_set_allocators);
		render_pass_map_init(&self->render_passes);
		shader_map_init(&self->shaders);
		program_map_init(&self->programs);
		framebuffer_allocator_init(&self->framebuffer_allocator, self);
		attachment_allocator_init(&self->transient_allocator, self);
	}

	/* Tear a Device down to the point where its storage can be freed. Runs the
	 * former ~Device prologue first (in the same order), then the teardown the
	 * implicit member destruction used to perform, deepest-owned last. */
	void Device::device_deinit(Device *self)
	{
		self->wait_idle();

		framebuffer_allocator_clear(&self->framebuffer_allocator);
		attachment_allocator_clear(&self->transient_allocator);
		for (unsigned i = 0; i < (unsigned)StockSampler_Count; i++)
			smh_reset(&self->samplers[i]);

		fencemanager_deinit(&self->managers.fence);
		semaphoremanager_deinit(&self->managers.semaphore);

		/* Remaining teardown in reverse member-declaration order, matching what the
		 * implicit member destructors did. Declaration order is handle_pool,
		 * managers, per_frame, the five VulkanCache, then the two allocators; so the
		 * destruction order is allocators, caches, per_frame, managers, handle_pool.
		 * In particular per_frame is destroyed AFTER the caches, as its declaration
		 * comment requires. */
		framebuffer_allocator_deinit(&self->framebuffer_allocator);
		attachment_allocator_deinit(&self->transient_allocator);
		program_map_deinit(&self->programs);
		shader_map_deinit(&self->shaders);
		render_pass_map_deinit(&self->render_passes);
		descriptor_set_allocator_map_deinit(&self->descriptor_set_allocators);
		pipeline_layout_map_deinit(&self->pipeline_layouts);
		per_frame_ptr_vec_deinit(&self->per_frame);
		bufferpool_deinit(&self->managers.vbo);
		bufferpool_deinit(&self->managers.ubo);
		deviceallocator_deinit(&self->managers.memory);
		/* Free the per-queue wait lists' backing storage. These plain structs have
		 * no destructor (Device is malloc'd, so no member dtor ran), and
		 * clear_wait_semaphores only resets counts, so free them explicitly here. */
		sem_handle_vec_deinit(&self->graphics.wait_semaphores);
		sem_handle_vec_deinit(&self->compute.wait_semaphores);
		sem_handle_vec_deinit(&self->transfer.wait_semaphores);
		VkPipelineStageVec_free_storage(&self->graphics.wait_stages);
		VkPipelineStageVec_free_storage(&self->compute.wait_stages);
		VkPipelineStageVec_free_storage(&self->transfer.wait_stages);
		self->handle_pool.deinit();
	}

	void Device::add_wait_semaphore_nolock(CommandBufferType type, Semaphore semaphore, VkPipelineStageFlags stages,
			bool flush)
	{
		VK_ASSERT(stages != 0);
		if (flush)
			flush_frame(type);
		QueueData &data = get_queue_data(type);

#ifdef VULKAN_DEBUG
		{ int _wi; for (_wi = 0; _wi < sem_handle_vec_size(&data.wait_semaphores); _wi++)
			VK_ASSERT(sem_get(sem_handle_vec_at(&data.wait_semaphores, _wi)) != sem_get(&semaphore)); }
#endif

		sem_handle_vec_push(&data.wait_semaphores, &semaphore);
		VkPipelineStageVec_push(&data.wait_stages, &stages);
		data.need_fence = true;

		// Sanity check.
		VK_ASSERT(sem_handle_vec_size(&data.wait_semaphores) < 16 * 1024);
	}

	Shader *Device::request_shader(const uint32_t *data, size_t size)
	{
		Hasher hasher; hasher_init(&hasher);
		hasher_data(&hasher, data, size);

		Hash hash = hasher_get(&hasher);
		Shader *ret = shader_map_find(&shaders, hash);
		if (!ret)
			ret = shader_map_emplace_yield(&shaders, hash, this, data, size);
		return ret;
	}

	Program *Device::request_program(Shader *compute)
	{
		Hasher hasher; hasher_init(&hasher);
		hasher_u64(&hasher, compute->intrusive_node.key);

		Hash hash = hasher_get(&hasher);
		Program *ret = program_map_find(&programs, hash);
		if (!ret)
			ret = program_map_emplace_yield_compute(&programs, hash, this, compute);
		return ret;
	}

	Program *Device::request_program(const uint32_t *compute_data, size_t compute_size)
	{
		Shader *compute = request_shader(compute_data, compute_size);
		return request_program(compute);
	}

	Program *Device::request_program(Shader *vertex, Shader *fragment)
	{
		Hasher hasher; hasher_init(&hasher);
		hasher_u64(&hasher, vertex->intrusive_node.key);
		hasher_u64(&hasher, fragment->intrusive_node.key);

		Hash hash = hasher_get(&hasher);
		Program *ret = program_map_find(&programs, hash);

		if (!ret)
			ret = program_map_emplace_yield_graphics(&programs, hash, this, vertex, fragment);
		return ret;
	}

	Program *Device::request_program(const uint32_t *vertex_data, size_t vertex_size, const uint32_t *fragment_data,
			size_t fragment_size)
	{
		Shader *vertex = request_shader(vertex_data, vertex_size);
		Shader *fragment = request_shader(fragment_data, fragment_size);
		return request_program(vertex, fragment);
	}

	PipelineLayout *Device::request_pipeline_layout(const CombinedResourceLayout &layout)
	{
		Hasher h; hasher_init(&h);
		hasher_data(&h, (const uint32_t *)(layout.sets), sizeof(layout.sets));
		hasher_data(&h, &layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
		hasher_u32(&h, layout.push_constant_range.stageFlags);
		hasher_u32(&h, layout.push_constant_range.size);
		hasher_data(&h, layout.spec_constant_mask, sizeof(layout.spec_constant_mask));
		hasher_u32(&h, layout.attribute_mask);
		hasher_u32(&h, layout.render_target_mask);

		Hash hash = hasher_get(&h);
		PipelineLayout *ret = pipeline_layout_map_find(&pipeline_layouts, hash);
		if (!ret)
			ret = pipeline_layout_map_emplace_yield(&pipeline_layouts, hash, this, layout);
		return ret;
	}

	DescriptorSetAllocator *Device::request_descriptor_set_allocator(const DescriptorSetLayout &layout, const uint32_t *stages_for_bindings)
	{
		Hasher h; hasher_init(&h);
		hasher_data(&h, (const uint32_t *)(&layout), sizeof(layout));
		hasher_data(&h, stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
		Hash hash = hasher_get(&h);

		DescriptorSetAllocator *ret = descriptor_set_allocator_map_find(&descriptor_set_allocators, hash);
		if (!ret)
			ret = descriptor_set_allocator_map_emplace_yield(&descriptor_set_allocators, hash, this, layout, stages_for_bindings);
		return ret;
	}

	void Device::bake_program(Program &program)
	{
		CombinedResourceLayout layout;
		if (program_get_shader(&program, ShaderStage_Vertex))
			layout.attribute_mask = shader_get_layout(program_get_shader(&program, ShaderStage_Vertex))->input_mask;
		if (program_get_shader(&program, ShaderStage_Fragment))
			layout.render_target_mask = shader_get_layout(program_get_shader(&program, ShaderStage_Fragment))->output_mask;

		layout.descriptor_set_mask = 0;

		for (unsigned i = 0; i < (unsigned)(ShaderStage_Count); i++)
		{
			const Shader *shader = program_get_shader(&program, (ShaderStage)(i));
			if (!shader)
				continue;

			uint32_t stage_mask = 1u << i;

			const ResourceLayout &shader_layout = *shader_get_layout(shader);
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
				layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
				layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
				layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
				layout.sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
				layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
				layout.sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
				layout.sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
				layout.sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

				FOR_EACH_BIT(shader_layout.sets[set].immutable_sampler_mask, binding)
				{
					StockSampler sampler = get_immutable_sampler(shader_layout.sets[set], binding);

					// Do we already have an immutable sampler? Make sure it matches the layout.
					if (has_immutable_sampler(layout.sets[set], binding))
					{
						if (sampler != get_immutable_sampler(layout.sets[set], binding))
							LOGE("Immutable sampler mismatch detected!\n");
					}

					set_immutable_sampler(layout.sets[set], binding, sampler);
				}

				uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask|
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_buffer_mask |
					shader_layout.sets[set].input_attachment_mask |
					shader_layout.sets[set].sampler_mask |
					shader_layout.sets[set].separate_image_mask;

				if (active_binds)
					layout.stages_for_sets[set] |= stage_mask;

				FOR_EACH_BIT(active_binds, bit)
				{
					layout.stages_for_bindings[set][bit] |= stage_mask;
				}
			}

			// Merge push constant ranges into one range.
			// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
			if (shader_layout.push_constant_size != 0)
			{
				layout.push_constant_range.stageFlags |= 1u << i;
				layout.push_constant_range.size =
					max_(layout.push_constant_range.size, shader_layout.push_constant_size);
			}

			layout.spec_constant_mask[i] = shader_layout.spec_constant_mask;
			layout.combined_spec_constant_mask |= shader_layout.spec_constant_mask;
		}

		for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
		{
			if (layout.stages_for_sets[i] != 0)
				layout.descriptor_set_mask |= 1u << i;
		}

		Hasher h; hasher_init(&h);
		hasher_u32(&h, layout.push_constant_range.stageFlags);
		hasher_u32(&h, layout.push_constant_range.size);
		layout.push_constant_layout_hash = hasher_get(&h);
		program_set_pipeline_layout(&program, request_pipeline_layout(layout));
	}

	void Device::set_context(const Context &context)
	{
		instance = context_get_instance(&context);
		gpu = context_get_gpu(&context);
		device = context_get_device(&context);

		graphics_queue_family_index = context_get_graphics_queue_family(&context);
		graphics_queue = context_get_graphics_queue(&context);
		compute_queue_family_index = context_get_compute_queue_family(&context);
		compute_queue = context_get_compute_queue(&context);
		transfer_queue_family_index = context_get_transfer_queue_family(&context);
		transfer_queue = context_get_transfer_queue(&context);

		mem_props = *context_get_mem_props(&context);
		gpu_props = *context_get_gpu_props(&context);

		init_workarounds();

		init_stock_samplers();

#ifdef ANDROID
		init_frame_contexts(3); // Android needs a bit more ... ;)
#else
		init_frame_contexts(2); // By default, regular double buffer between CPU and GPU.
#endif

		ext = *context_get_enabled_device_features(&context);

		deviceallocator_init(&managers.memory, gpu, device);
		deviceallocator_set_supports_dedicated_allocation(&managers.memory, ext.supports_dedicated);
		semaphoremanager_init(&managers.semaphore, device);
		fencemanager_init(&managers.fence, device);
		bufferpool_init(&managers.vbo, this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		bufferpool_init(&managers.ubo, this, 256 * 1024, max_((VkDeviceSize)16u, gpu_props.limits.minUniformBufferOffsetAlignment),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
	}

	void Device::init_stock_samplers()
	{
		SamplerCreateInfo info = {};
		info.max_lod = VK_LOD_CLAMP_NONE;
		info.max_anisotropy = 1.0f;

		for (unsigned i = 0; i < (unsigned)(StockSampler_Count); i++)
		{
			StockSampler mode = (StockSampler)(i);

			switch (mode)
			{
				case StockSampler_NearestShadow:
				case StockSampler_LinearShadow:
					info.compare_enable = true;
					info.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
					break;

				default:
					info.compare_enable = false;
					break;
			}

			switch (mode)
			{
				case StockSampler_TrilinearClamp:
				case StockSampler_TrilinearWrap:
					info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
					break;

				default:
					info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
					break;
			}

			switch (mode)
			{
				case StockSampler_LinearClamp:
				case StockSampler_LinearWrap:
				case StockSampler_TrilinearClamp:
				case StockSampler_TrilinearWrap:
				case StockSampler_LinearShadow:
					info.mag_filter = VK_FILTER_LINEAR;
					info.min_filter = VK_FILTER_LINEAR;
					break;

				default:
					info.mag_filter = VK_FILTER_NEAREST;
					info.min_filter = VK_FILTER_NEAREST;
					break;
			}

			switch (mode)
			{
				default:
				case StockSampler_LinearWrap:
				case StockSampler_NearestWrap:
				case StockSampler_TrilinearWrap:
					info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
					break;

				case StockSampler_LinearClamp:
				case StockSampler_NearestClamp:
				case StockSampler_TrilinearClamp:
				case StockSampler_NearestShadow:
				case StockSampler_LinearShadow:
					info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
					info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
					info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
					break;
			}
			samplers[i] = create_sampler(info, mode);
		}
	}

	static void request_block(Device &device, struct BufferBlock *block, VkDeviceSize size,
			struct BufferPool *pool, struct BufferBlockVec *dma, struct BufferBlockVec *recycle)
	{
		if (block->mapped)
			device.unmap_host_buffer(*bh_get(&block->cpu), MEMORY_ACCESS_WRITE_BIT);

		if (block->offset == 0)
		{
			if (block->size == bufferpool_get_block_size(pool))
				bufferpool_recycle_block(pool, block); /* copy-retains; we fini below */
		}
		else
		{
			if (bh_get(&block->cpu) != bh_get(&block->gpu))
			{
				VK_ASSERT(dma);
				bufferblock_vec_push(dma, block);
			}

			if (block->size == bufferpool_get_block_size(pool))
				bufferblock_vec_push(recycle, block);
		}

		/* Drop this block's refs (it was recycled/staged by copy above) before
		 * overwriting it with the next request or emptying it. */
		bufferblock_fini(block);
		if (size)
		{
			struct BufferBlock produced = bufferpool_request_block(pool, size);
			bufferblock_steal(block, &produced);
		}
		else
			bufferblock_init(block);
	}

	void Device::request_vertex_block_nolock(BufferBlock &block, VkDeviceSize size)
	{
		request_block(*this, &block, size, &managers.vbo, &dma.vbo, &frame().vbo_blocks);
	}

	void Device::request_uniform_block_nolock(BufferBlock &block, VkDeviceSize size)
	{
		request_block(*this, &block, size, &managers.ubo, &dma.ubo, &frame().ubo_blocks);
	}

	void Device::submit(CommandBufferHandle &cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
	{
		/* Move ownership of cmd into submit_nolock's by-value parameter and null
		 * the caller's handle, matching the old move-from semantics. A plain
		 * struct copy would otherwise leave the caller's handle aliasing the
		 * same pointee, so a later cbh_reset on it would double-release. */
		CommandBufferHandle moved;
		cbh_steal(&moved, &cmd);
		submit_nolock(moved, fence, semaphore_count, semaphores);
	}

	CommandBufferType Device::get_physical_queue_type(CommandBufferType queue_type) const
	{
		if (queue_type != Type_AsyncGraphics)
		{
			return queue_type;
		}
		else
		{
			if (graphics_queue_family_index == compute_queue_family_index && graphics_queue != compute_queue)
				return Type_AsyncCompute;
			else
				return Type_Generic;
		}
	}

	void Device::submit_nolock(CommandBufferHandle cmd, Fence *fence, unsigned semaphore_count, Semaphore *semaphores)
	{
		CommandBufferType type = commandbuffer_get_command_buffer_type(cbh_get(&cmd));
		CommandPool *pool = get_command_pool(type);
		CommandBufferHandleVec *submissions = get_queue_submissions(type);

		command_pool_signal_submitted(pool, commandbuffer_get_command_buffer(cbh_get(&cmd)));
		commandbuffer_end(cbh_get(&cmd));
		cbhvec_push(submissions, &cmd);

		VkFence cleared_fence = VK_NULL_HANDLE;

		if (fence || semaphore_count)
			submit_queue(type, fence ? &cleared_fence : NULL, semaphore_count, semaphores);

		if (fence)
		{
			VK_ASSERT(!fence_is_valid(fence));
			{ struct FenceHolder *_fh = (struct FenceHolder *)object_pool_raw_allocate(&handle_pool.fences); fenceholder_init(_fh, this, cleared_fence); *fence = fence_make(_fh); }
		}

		decrement_frame_counter_nolock();
	}

	POD_VEC_DECLARE(VkFlagsVec, VkFlags);
	void Device::submit_empty_inner(CommandBufferType type, VkFence *fence,
			unsigned semaphore_count, Semaphore *semaphores)
	{
		QueueData &data = get_queue_data(type);
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

		// Add external wait semaphores.
		SemaphoreVec waits   = { NULL, 0, 0 };
		SemaphoreVec signals = { NULL, 0, 0 };
		VkFlagsVec stages      = { NULL, 0, 0 };
		{
			size_t ws;
			for (ws = 0; ws < VkPipelineStageVec_size(&data.wait_stages); ws++)
				VkFlagsVec_push(&stages, VkPipelineStageVec_at(&data.wait_stages, ws));
		}

		{ int _wi; for (_wi = 0; _wi < sem_handle_vec_size(&data.wait_semaphores); _wi++)
		{
			Semaphore *semaphore = sem_handle_vec_at(&data.wait_semaphores, _wi);
			VkSemaphore wait = semaphoreholder_consume(sem_get(semaphore));
			SemaphoreVec_push(&frame().recycled_semaphores, &wait);
			SemaphoreVec_push(&waits, &wait);
		} }
		VkPipelineStageVec_clear(&data.wait_stages);
		sem_handle_vec_clear(&data.wait_semaphores);

		// Add external signal semaphores.
		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VkSemaphore cleared_semaphore = semaphoremanager_request_cleared_semaphore(&managers.semaphore);
			SemaphoreVec_push(&signals, &cleared_semaphore);
			VK_ASSERT(!sem_is_valid(&semaphores[i]));
			{ struct SemaphoreHolder *_sh = (struct SemaphoreHolder *)object_pool_raw_allocate(&handle_pool.semaphores); semaphoreholder_init(_sh, this, cleared_semaphore, true); semaphores[i] = sem_make(_sh); }
		}

		submit.signalSemaphoreCount = SemaphoreVec_size(&signals);
		submit.waitSemaphoreCount = SemaphoreVec_size(&waits);
		if (!SemaphoreVec_empty(&signals))
			submit.pSignalSemaphores = SemaphoreVec_data(&signals);
		if (!VkFlagsVec_empty(&stages))
			submit.pWaitDstStageMask = VkFlagsVec_data(&stages);
		if (!SemaphoreVec_empty(&waits))
			submit.pWaitSemaphores = SemaphoreVec_data(&waits);

		VkQueue queue;
		switch (type)
		{
			default:
			case Type_Generic:
				queue = graphics_queue;
				break;
			case Type_AsyncCompute:
				queue = compute_queue;
				break;
			case Type_AsyncTransfer:
				queue = transfer_queue;
				break;
		}

		VkFence cleared_fence = fence ? fencemanager_request_cleared_fence(&managers.fence) : VK_NULL_HANDLE;
		VkResult result = vkQueueSubmit(queue, 1, &submit, cleared_fence);

		if (result != VK_SUCCESS)
			LOGE("vkQueueSubmit failed (code: %d).\n", int(result));

		SemaphoreVec_free_storage(&waits);
		SemaphoreVec_free_storage(&signals);
		VkFlagsVec_free_storage(&stages);

		if (fence)
		{
			if (result == VK_SUCCESS)
			{
				/* See submit_queue(): never enqueue a fence that was handed to a
				 * failed submit - it will never signal and would hang the frame's
				 * vkWaitForFences. Recycle it and return a null fence instead. */
				FenceVec_push(&frame().wait_fences, &cleared_fence);
				*fence = cleared_fence;
			}
			else
			{
				if (cleared_fence != VK_NULL_HANDLE)
					fencemanager_recycle_fence(&managers.fence, cleared_fence);
				*fence = VK_NULL_HANDLE;
			}
			data.need_fence = false;
		}
		else
			data.need_fence = true;
	}

	void Device::submit_staging(CommandBufferHandle &cmd, VkBufferUsageFlags usage, bool flush)
	{
		VkAccessFlags access = buffer_usage_to_possible_access(usage);
		VkPipelineStageFlags stages = buffer_usage_to_possible_stages(usage);

		if (transfer_queue == graphics_queue && transfer_queue == compute_queue)
		{
			// For single-queue systems, just use a pipeline barrier.
			commandbuffer_barrier_simple(cbh_get(&cmd), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
			{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 0, NULL); }
		}
		else
		{
			VkPipelineStageFlags compute_stages = stages &
				(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				 VK_PIPELINE_STAGE_TRANSFER_BIT |
				 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

			VkAccessFlags compute_access = access &
				(VK_ACCESS_SHADER_READ_BIT |
				 VK_ACCESS_SHADER_WRITE_BIT |
				 VK_ACCESS_TRANSFER_READ_BIT |
				 VK_ACCESS_UNIFORM_READ_BIT |
				 VK_ACCESS_TRANSFER_WRITE_BIT |
				 VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

			VkPipelineStageFlags graphics_stages = stages;

			if (transfer_queue == graphics_queue)
			{
				commandbuffer_barrier_simple(cbh_get(&cmd), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						graphics_stages, access);

				if (compute_stages != 0)
				{
					Semaphore sem; sem.data = NULL;
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 1, &sem); }
					add_wait_semaphore_nolock(Type_AsyncCompute, sem, compute_stages, flush);
					sem_reset(&sem);
				}
				else
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 0, NULL); }
			}
			else if (transfer_queue == compute_queue)
			{
				commandbuffer_barrier_simple(cbh_get(&cmd), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
						compute_stages, compute_access);

				if (graphics_stages != 0)
				{
					Semaphore sem; sem.data = NULL;
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 1, &sem); }
					add_wait_semaphore_nolock(Type_Generic, sem, graphics_stages, flush);
					sem_reset(&sem);
				}
				else
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 0, NULL); }
			}
			else
			{
				if (graphics_stages != 0 && compute_stages != 0)
				{
					Semaphore semaphores[2];
					semaphores[0].data = NULL;
					semaphores[1].data = NULL;
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 2, semaphores); }
					add_wait_semaphore_nolock(Type_Generic, semaphores[0], graphics_stages, flush);
					add_wait_semaphore_nolock(Type_AsyncCompute, semaphores[1], compute_stages, flush);
					sem_reset(&semaphores[0]);
					sem_reset(&semaphores[1]);
				}
				else if (graphics_stages != 0)
				{
					Semaphore sem; sem.data = NULL;
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 1, &sem); }
					add_wait_semaphore_nolock(Type_Generic, sem, graphics_stages, flush);
					sem_reset(&sem);
				}
				else if (compute_stages != 0)
				{
					Semaphore sem; sem.data = NULL;
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 1, &sem); }
					add_wait_semaphore_nolock(Type_AsyncCompute, sem, compute_stages, flush);
					sem_reset(&sem);
				}
				else
					{ CommandBufferHandle _m; cbh_steal(&_m, &cmd); submit_nolock(_m, NULL, 0, NULL); }
			}
		}
	}

	POD_VEC_DECLARE(VkSubmitInfoVec, VkSubmitInfo);

	void Device::submit_queue(CommandBufferType type, VkFence *fence,
			unsigned semaphore_count, Semaphore *semaphores)
	{
		type = get_physical_queue_type(type);

		// Always check if we need to flush pending transfers.
		if (type != Type_AsyncTransfer)
			flush_frame(Type_AsyncTransfer);

		QueueData &data = get_queue_data(type);
		CommandBufferHandleVec *submissions = get_queue_submissions(type);

		if (cbhvec_empty(submissions))
		{
			if (fence || semaphore_count)
				submit_empty_inner(type, fence, semaphore_count, semaphores);
			return;
		}

		CommandBufferVec cmds = { NULL, 0, 0 };

		VkSubmitInfoVec submits = { NULL, 0, 0 };
		size_t last_cmd = 0;

		SemaphoreVec waits[2]   = { { NULL, 0, 0 }, { NULL, 0, 0 } };
		SemaphoreVec signals[2] = { { NULL, 0, 0 }, { NULL, 0, 0 } };
		VkFlagsVec stages[2]      = { { NULL, 0, 0 }, { NULL, 0, 0 } };

		// Add external wait semaphores.
		{
			// Move the pending wait stages across (then the source is cleared below).
			size_t ws;
			for (ws = 0; ws < VkPipelineStageVec_size(&data.wait_stages); ws++)
				VkFlagsVec_push(&stages[0], VkPipelineStageVec_at(&data.wait_stages, ws));
		}

		{ int _wi; for (_wi = 0; _wi < sem_handle_vec_size(&data.wait_semaphores); _wi++)
		{
			Semaphore *semaphore = sem_handle_vec_at(&data.wait_semaphores, _wi);
			VkSemaphore wait = semaphoreholder_consume(sem_get(semaphore));
			SemaphoreVec_push(&frame().recycled_semaphores, &wait);
			SemaphoreVec_push(&waits[0], &wait);
		} }
		VkPipelineStageVec_clear(&data.wait_stages);
		sem_handle_vec_clear(&data.wait_semaphores);

		{ int _si; for (_si = 0; _si < submissions->count; _si++)
		{
			CommandBufferHandle *cmd = &submissions->items[_si];
			VkCommandBuffer _cb = commandbuffer_get_command_buffer(cbh_get(cmd));
			CommandBufferVec_push(&cmds, &_cb);
		} }

		if (CommandBufferVec_size(&cmds) > (int)last_cmd)
		{
			// Push all pending cmd buffers to their own submission.
			VkSubmitInfo zero_submit;
			memset(&zero_submit, 0, sizeof(zero_submit));
			VkSubmitInfoVec_push(&submits, &zero_submit);

			VkSubmitInfo &submit = *VkSubmitInfoVec_back(&submits);
			submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit.pNext = NULL;
			submit.commandBufferCount = CommandBufferVec_size(&cmds) - last_cmd;
			submit.pCommandBuffers = CommandBufferVec_data(&cmds) + last_cmd;
			last_cmd = CommandBufferVec_size(&cmds);
		}

		VkFence cleared_fence = fence ? fencemanager_request_cleared_fence(&managers.fence) : VK_NULL_HANDLE;

		for (unsigned i = 0; i < semaphore_count; i++)
		{
			VkSemaphore cleared_semaphore = semaphoremanager_request_cleared_semaphore(&managers.semaphore);
			SemaphoreVec_push(&signals[VkSubmitInfoVec_size(&submits) - 1], &cleared_semaphore);
			VK_ASSERT(!sem_is_valid(&semaphores[i]));
			{ struct SemaphoreHolder *_sh = (struct SemaphoreHolder *)object_pool_raw_allocate(&handle_pool.semaphores); semaphoreholder_init(_sh, this, cleared_semaphore, true); semaphores[i] = sem_make(_sh); }
		}

		for (int i = 0; i < VkSubmitInfoVec_size(&submits); i++)
		{
			VkSubmitInfo &submit = *VkSubmitInfoVec_at(&submits, i);
			submit.waitSemaphoreCount = SemaphoreVec_size(&waits[i]);
			if (!SemaphoreVec_empty(&waits[i]))
			{
				submit.pWaitSemaphores = SemaphoreVec_data(&waits[i]);
				submit.pWaitDstStageMask = VkFlagsVec_data(&stages[i]);
			}

			submit.signalSemaphoreCount = SemaphoreVec_size(&signals[i]);
			if (!SemaphoreVec_empty(&signals[i]))
				submit.pSignalSemaphores = SemaphoreVec_data(&signals[i]);
		}

		VkQueue queue;
		switch (type)
		{
			default:
			case Type_Generic:
				queue = graphics_queue;
				break;
			case Type_AsyncCompute:
				queue = compute_queue;
				break;
			case Type_AsyncTransfer:
				queue = transfer_queue;
				break;
		}

		VkResult result = vkQueueSubmit(queue, VkSubmitInfoVec_size(&submits), VkSubmitInfoVec_data(&submits), cleared_fence);
		if (result != VK_SUCCESS)
			LOGE("vkQueueSubmit failed (code: %d).\n", int(result));
		cbhvec_clear(submissions);

		CommandBufferVec_free_storage(&cmds);
		VkSubmitInfoVec_free_storage(&submits);
		SemaphoreVec_free_storage(&waits[0]);  SemaphoreVec_free_storage(&waits[1]);
		SemaphoreVec_free_storage(&signals[0]); SemaphoreVec_free_storage(&signals[1]);
		VkFlagsVec_free_storage(&stages[0]);  VkFlagsVec_free_storage(&stages[1]);

		if (fence)
		{
			if (result == VK_SUCCESS)
			{
				/* Only wait on a fence that is actually associated with submitted
				 * work. If the submit failed the fence will never be signalled, so
				 * enqueuing it for the next PerFrame::begin() would wedge that frame
				 * in vkWaitForFences(UINT64_MAX) forever. Recycle it instead and hand
				 * the caller a null fence. */
				FenceVec_push(&frame().wait_fences, &cleared_fence);
				*fence = cleared_fence;
			}
			else
			{
				if (cleared_fence != VK_NULL_HANDLE)
					fencemanager_recycle_fence(&managers.fence, cleared_fence);
				*fence = VK_NULL_HANDLE;
			}
			data.need_fence = false;
		}
		else
			data.need_fence = true;
	}

	void Device::sync_buffer_blocks()
	{
		if (bufferblock_vec_empty(&dma.vbo) && bufferblock_vec_empty(&dma.ubo))
			return;

		VkBufferUsageFlags usage = 0;

		CommandBufferHandle cmd = request_command_buffer_nolock(Type_AsyncTransfer);

		commandbuffer_begin_region(cbh_get(&cmd), "buffer-block-sync");

		{
			int _bi;
			for (_bi = 0; _bi < dma.vbo.count; _bi++)
			{
				struct BufferBlock *block = &dma.vbo.items[_bi];
				VK_ASSERT(block->offset != 0);
				commandbuffer_copy_buffer(cbh_get(&cmd), *bh_get(&block->gpu), 0, *bh_get(&block->cpu), 0, block->offset);
				usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			}
			for (_bi = 0; _bi < dma.ubo.count; _bi++)
			{
				struct BufferBlock *block = &dma.ubo.items[_bi];
				VK_ASSERT(block->offset != 0);
				commandbuffer_copy_buffer(cbh_get(&cmd), *bh_get(&block->gpu), 0, *bh_get(&block->cpu), 0, block->offset);
				usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			}
		}

		bufferblock_vec_clear(&dma.vbo);
		bufferblock_vec_clear(&dma.ubo);

		commandbuffer_end_region(cbh_get(&cmd));

		// Do not flush graphics or compute in this context.
		// We must be able to inject semaphores into all currently enqueued graphics / compute.
		submit_staging(cmd, usage, false);
	}

	void Device::end_frame_nolock()
	{
		// Make sure we have a fence which covers all submissions in the frame.
		VkFence fence;

		if (transfer.need_fence || !cbhvec_empty(&frame().transfer_submissions))
		{
			fence = VK_NULL_HANDLE;
			submit_queue(Type_AsyncTransfer, &fence, 0, NULL);
			if (fence != VK_NULL_HANDLE)
				FenceVec_push(&frame().recycle_fences, &fence);
			transfer.need_fence = false;
		}

		if (graphics.need_fence || !cbhvec_empty(&frame().graphics_submissions))
		{
			fence = VK_NULL_HANDLE;
			submit_queue(Type_Generic, &fence, 0, NULL);
			if (fence != VK_NULL_HANDLE)
				FenceVec_push(&frame().recycle_fences, &fence);
			graphics.need_fence = false;
		}

		if (compute.need_fence || !cbhvec_empty(&frame().compute_submissions))
		{
			fence = VK_NULL_HANDLE;
			submit_queue(Type_AsyncCompute, &fence, 0, NULL);
			if (fence != VK_NULL_HANDLE)
				FenceVec_push(&frame().recycle_fences, &fence);
			compute.need_fence = false;
		}
	}

	void Device::flush_frame_nolock()
	{
		flush_frame(Type_AsyncTransfer);
		flush_frame(Type_Generic);
		flush_frame(Type_AsyncCompute);
	}

	Device::QueueData &Device::get_queue_data(CommandBufferType type)
	{
		switch (get_physical_queue_type(type))
		{
			default:
			case Type_Generic:
				return graphics;
			case Type_AsyncCompute:
				return compute;
			case Type_AsyncTransfer:
				return transfer;
		}
	}

	CommandPool *Device::get_command_pool(CommandBufferType type)
	{
		switch (get_physical_queue_type(type))
		{
			default:
			case Type_Generic:
				return &frame().graphics_cmd_pool;
			case Type_AsyncCompute:
				return &frame().compute_cmd_pool;
			case Type_AsyncTransfer:
				return &frame().transfer_cmd_pool;
		}
	}

	Device::CommandBufferHandleVec *Device::get_queue_submissions(CommandBufferType type)
	{
		switch (get_physical_queue_type(type))
		{
			default:
			case Type_Generic:
				return &frame().graphics_submissions;
			case Type_AsyncCompute:
				return &frame().compute_submissions;
			case Type_AsyncTransfer:
				return &frame().transfer_submissions;
		}
	}

	CommandBufferHandle Device::request_command_buffer(CommandBufferType type)
	{
		return request_command_buffer_nolock(type);
	}

	CommandBufferHandle Device::request_command_buffer_nolock(CommandBufferType type)
	{
		VkCommandBuffer cmd = command_pool_request_command_buffer(get_command_pool(type));

		VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &info);
		add_frame_counter_nolock();
		struct CommandBuffer *_cb = (struct CommandBuffer *)object_pool_raw_allocate(&handle_pool.command_buffers); commandbuffer_init(_cb, this, cmd, type); CommandBufferHandle handle = cbh_make(_cb);
		return handle;
	}

	void Device::init_frame_contexts(unsigned count)
	{
		wait_idle_nolock();

		// Clear out caches which might contain stale data from now on.
		framebuffer_allocator_clear(&framebuffer_allocator);
		attachment_allocator_clear(&transient_allocator);
		per_frame_ptr_vec_clear(&per_frame);

		for (unsigned i = 0; i < count; i++)
		{
			PerFrame *pf = (PerFrame *)malloc(sizeof(PerFrame));
			per_frame_init(pf, this);
			per_frame_ptr_vec_push(&per_frame, pf);
		}
	}

	void per_frame_init(struct Device::PerFrame *self, Device *device)
	{
		self->device = device->get_device();
		self->managers = &device->managers;
		command_pool_init(&self->graphics_cmd_pool, device->get_device(), device->graphics_queue_family_index);
		command_pool_init(&self->compute_cmd_pool, device->get_device(), device->compute_queue_family_index);
		command_pool_init(&self->transfer_cmd_pool, device->get_device(), device->transfer_queue_family_index);
		/* vbo/ubo block lists: plain structs, init explicitly. */
		bufferblock_vec_init(&self->vbo_blocks);
		bufferblock_vec_init(&self->ubo_blocks);
		/* POD_VEC members have no constructor; zero-initialise them. */
		memset(&self->wait_fences, 0, sizeof(self->wait_fences));
		memset(&self->recycle_fences, 0, sizeof(self->recycle_fences));
		memset(&self->allocations, 0, sizeof(self->allocations));
		memset(&self->destroyed_framebuffers, 0, sizeof(self->destroyed_framebuffers));
		memset(&self->destroyed_samplers, 0, sizeof(self->destroyed_samplers));
		memset(&self->destroyed_pipelines, 0, sizeof(self->destroyed_pipelines));
		memset(&self->destroyed_image_views, 0, sizeof(self->destroyed_image_views));
		memset(&self->destroyed_buffer_views, 0, sizeof(self->destroyed_buffer_views));
		memset(&self->destroyed_images, 0, sizeof(self->destroyed_images));
		memset(&self->destroyed_buffers, 0, sizeof(self->destroyed_buffers));
		memset(&self->recycled_semaphores, 0, sizeof(self->recycled_semaphores));
		memset(&self->destroyed_semaphores, 0, sizeof(self->destroyed_semaphores));
		cbhvec_init(&self->graphics_submissions);
		cbhvec_init(&self->compute_submissions);
		cbhvec_init(&self->transfer_submissions);
	}

	void Device::free_memory_nolock(const DeviceAllocation &alloc)
	{
		DeviceAllocationVec_push(&frame().allocations, &alloc);
	}

#ifdef VULKAN_DEBUG

	/* Debug-only check that a value is not already queued in a POD_VEC (the
	 * double-destroy guard that used the exists<>() template). C has no function
	 * templates, so this is a macro doing the linear scan inline over the vec's
	 * items/count; it works for any element type comparable with ==. */
#define VK_ASSERT_NOT_IN_VEC(vec, value)                                       \
	do                                                                         \
	{                                                                          \
		int _vk_i;                                                             \
		for (_vk_i = 0; _vk_i < (vec).count; _vk_i++)                          \
		{                                                                      \
			if ((vec).items[_vk_i] == (value))                                 \
			{                                                                  \
				LOGE("Vulkan error at %s:%d.\n", __FILE__, __LINE__);          \
				abort();                                                       \
			}                                                                  \
		}                                                                      \
	} while (0)

#else
#define VK_ASSERT_NOT_IN_VEC(vec, value) ((void)0)
#endif

	void Device::reset_fence(VkFence fence)
	{
		FenceVec_push(&frame().recycle_fences, &fence);
	}

	void Device::destroy_pipeline_nolock(VkPipeline pipeline)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_pipelines, pipeline);
		VkPipelineVec_push(&frame().destroyed_pipelines, &pipeline);
	}

	void Device::destroy_image_view_nolock(VkImageView view)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_image_views, view);
		RenderTargetViewVec_push(&frame().destroyed_image_views, &view);
	}

	void Device::destroy_buffer_view_nolock(VkBufferView view)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_buffer_views, view);
		VkBufferViewVec_push(&frame().destroyed_buffer_views, &view);
	}

	void Device::destroy_semaphore_nolock(VkSemaphore semaphore)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_semaphores, semaphore);
		SemaphoreVec_push(&frame().destroyed_semaphores, &semaphore);
	}

	void Device::destroy_image_nolock(VkImage image)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_images, image);
		VkImageVec_push(&frame().destroyed_images, &image);
	}

	void Device::destroy_buffer_nolock(VkBuffer buffer)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_buffers, buffer);
		VkBufferVec_push(&frame().destroyed_buffers, &buffer);
	}

	void Device::destroy_sampler_nolock(VkSampler sampler)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_samplers, sampler);
		VkSamplerVec_push(&frame().destroyed_samplers, &sampler);
	}

	void Device::destroy_framebuffer_nolock(VkFramebuffer framebuffer)
	{
		VK_ASSERT_NOT_IN_VEC(frame().destroyed_framebuffers, framebuffer);
		VkFramebufferVec_push(&frame().destroyed_framebuffers, &framebuffer);
	}

	void Device::clear_wait_semaphores()
	{
		{ int _wi;
		for (_wi = 0; _wi < sem_handle_vec_size(&graphics.wait_semaphores); _wi++)
			vkDestroySemaphore(device, semaphoreholder_consume(sem_get(sem_handle_vec_at(&graphics.wait_semaphores, _wi))), NULL);
		for (_wi = 0; _wi < sem_handle_vec_size(&compute.wait_semaphores); _wi++)
			vkDestroySemaphore(device, semaphoreholder_consume(sem_get(sem_handle_vec_at(&compute.wait_semaphores, _wi))), NULL);
		for (_wi = 0; _wi < sem_handle_vec_size(&transfer.wait_semaphores); _wi++)
			vkDestroySemaphore(device, semaphoreholder_consume(sem_get(sem_handle_vec_at(&transfer.wait_semaphores, _wi))), NULL);
		}

		sem_handle_vec_clear(&graphics.wait_semaphores);
		VkPipelineStageVec_clear(&graphics.wait_stages);
		sem_handle_vec_clear(&compute.wait_semaphores);
		VkPipelineStageVec_clear(&compute.wait_stages);
		sem_handle_vec_clear(&transfer.wait_semaphores);
		VkPipelineStageVec_clear(&transfer.wait_stages);
	}

	void Device::wait_idle_nolock()
	{
		if (per_frame.count != 0)
			end_frame_nolock();

		if (device != VK_NULL_HANDLE)
			vkDeviceWaitIdle(device);

		clear_wait_semaphores();

		// Free memory for buffer pools.
		bufferpool_reset(&managers.vbo);
		bufferpool_reset(&managers.ubo);
		{ int _pi; for (_pi = 0; _pi < per_frame.count; _pi++)
		{
			PerFrame *frame = per_frame.items[_pi];
			bufferblock_vec_clear(&frame->vbo_blocks);
			bufferblock_vec_clear(&frame->ubo_blocks);
		} }

		framebuffer_allocator_clear(&framebuffer_allocator);
		attachment_allocator_clear(&transient_allocator);
		{
			struct IntrusiveListNode *n;
			for (n = descriptor_set_allocator_map_begin(&descriptor_set_allocators); n; n = n->next)
				descriptor_set_allocator_clear(descriptor_set_allocator_map_iter_get(n));
		}

		{ int _pi; for (_pi = 0; _pi < per_frame.count; _pi++)
		{
			PerFrame *frame = per_frame.items[_pi];
			// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
			FenceVec_clear(&frame->wait_fences);
			per_frame_begin(frame);
		} }
	}

	void Device::next_frame_context()
	{
		// Flush the frame here as we might have pending staging command buffers from init stage.
		end_frame_nolock();

		framebuffer_allocator_begin_frame(&framebuffer_allocator);
		attachment_allocator_begin_frame(&transient_allocator);
		{
			struct IntrusiveListNode *n;
			for (n = descriptor_set_allocator_map_begin(&descriptor_set_allocators); n; n = n->next)
				descriptor_set_allocator_begin_frame(descriptor_set_allocator_map_iter_get(n));
		}

		VK_ASSERT(per_frame.count != 0);
		frame_context_index++;
		if (frame_context_index >= (unsigned)per_frame.count)
			frame_context_index = 0;

		per_frame_begin(&frame());
	}

	void per_frame_begin(struct Device::PerFrame *self)
	{
		if (!FenceVec_empty(&self->wait_fences))
		{
			vkWaitForFences(self->device, FenceVec_size(&self->wait_fences), FenceVec_data(&self->wait_fences), VK_TRUE, UINT64_MAX);
			FenceVec_clear(&self->wait_fences);
		}

		if (!FenceVec_empty(&self->recycle_fences))
		{
			int _i;
			vkResetFences(self->device, FenceVec_size(&self->recycle_fences), FenceVec_data(&self->recycle_fences));
			for (_i = 0; _i < FenceVec_size(&self->recycle_fences); _i++)
			{
				VkFence &fence = *FenceVec_at(&self->recycle_fences, _i);
				fencemanager_recycle_fence(&self->managers->fence, fence);
			}
			FenceVec_clear(&self->recycle_fences);
		}

		command_pool_begin(&self->graphics_cmd_pool);
		command_pool_begin(&self->compute_cmd_pool);
		command_pool_begin(&self->transfer_cmd_pool);

		{
			int _i;
			for (_i = 0; _i < VkFramebufferVec_size(&self->destroyed_framebuffers); _i++)
				vkDestroyFramebuffer(self->device, *VkFramebufferVec_at(&self->destroyed_framebuffers, _i), NULL);
			for (_i = 0; _i < VkSamplerVec_size(&self->destroyed_samplers); _i++)
				vkDestroySampler(self->device, *VkSamplerVec_at(&self->destroyed_samplers, _i), NULL);
			for (_i = 0; _i < VkPipelineVec_size(&self->destroyed_pipelines); _i++)
				vkDestroyPipeline(self->device, *VkPipelineVec_at(&self->destroyed_pipelines, _i), NULL);
			for (_i = 0; _i < RenderTargetViewVec_size(&self->destroyed_image_views); _i++)
				vkDestroyImageView(self->device, *RenderTargetViewVec_at(&self->destroyed_image_views, _i), NULL);
			for (_i = 0; _i < VkBufferViewVec_size(&self->destroyed_buffer_views); _i++)
				vkDestroyBufferView(self->device, *VkBufferViewVec_at(&self->destroyed_buffer_views, _i), NULL);
			for (_i = 0; _i < VkImageVec_size(&self->destroyed_images); _i++)
				vkDestroyImage(self->device, *VkImageVec_at(&self->destroyed_images, _i), NULL);
			for (_i = 0; _i < VkBufferVec_size(&self->destroyed_buffers); _i++)
				vkDestroyBuffer(self->device, *VkBufferVec_at(&self->destroyed_buffers, _i), NULL);
		}
		{
			int _i;
			for (_i = 0; _i < SemaphoreVec_size(&self->destroyed_semaphores); _i++)
				vkDestroySemaphore(self->device, *SemaphoreVec_at(&self->destroyed_semaphores, _i), NULL);
		}
		{
			int _i;
			for (_i = 0; _i < SemaphoreVec_size(&self->recycled_semaphores); _i++)
				semaphoremanager_recycle(&self->managers->semaphore, *SemaphoreVec_at(&self->recycled_semaphores, _i));
		}
		{
			int _i;
			for (_i = 0; _i < DeviceAllocationVec_size(&self->allocations); _i++)
				deviceallocation_free_immediate_alloc(DeviceAllocationVec_at(&self->allocations, _i), &self->managers->memory);
		}

		{
			int _bi;
			for (_bi = 0; _bi < self->vbo_blocks.count; _bi++)
				bufferpool_recycle_block(&self->managers->vbo, &self->vbo_blocks.items[_bi]);
			for (_bi = 0; _bi < self->ubo_blocks.count; _bi++)
				bufferpool_recycle_block(&self->managers->ubo, &self->ubo_blocks.items[_bi]);
		}
		/* recycle_block copy-retained each block; clear() finis the originals,
		 * net-transferring the refs into the pools. */
		bufferblock_vec_clear(&self->vbo_blocks);
		bufferblock_vec_clear(&self->ubo_blocks);

		VkFramebufferVec_clear(&self->destroyed_framebuffers);
		VkSamplerVec_clear(&self->destroyed_samplers);
		VkPipelineVec_clear(&self->destroyed_pipelines);
		RenderTargetViewVec_clear(&self->destroyed_image_views);
		VkBufferViewVec_clear(&self->destroyed_buffer_views);
		VkImageVec_clear(&self->destroyed_images);
		VkBufferVec_clear(&self->destroyed_buffers);
		SemaphoreVec_clear(&self->destroyed_semaphores);
		SemaphoreVec_clear(&self->recycled_semaphores);
		DeviceAllocationVec_clear(&self->allocations);
	}

	void per_frame_fini(struct Device::PerFrame *self)
	{
		per_frame_begin(self);
		/* The command pools no longer self-destruct (their dtor became
		 * command_pool_deinit); tear them down explicitly, as the implicit member
		 * destruction used to. */
		command_pool_deinit(&self->graphics_cmd_pool);
		command_pool_deinit(&self->compute_cmd_pool);
		command_pool_deinit(&self->transfer_cmd_pool);
		/* Release the POD_VEC backing storage (begin() only resets the counts). */
		FenceVec_free_storage(&self->wait_fences);
		FenceVec_free_storage(&self->recycle_fences);
		DeviceAllocationVec_free_storage(&self->allocations);
		VkFramebufferVec_free_storage(&self->destroyed_framebuffers);
		VkSamplerVec_free_storage(&self->destroyed_samplers);
		VkPipelineVec_free_storage(&self->destroyed_pipelines);
		RenderTargetViewVec_free_storage(&self->destroyed_image_views);
		VkBufferViewVec_free_storage(&self->destroyed_buffer_views);
		VkImageVec_free_storage(&self->destroyed_images);
		VkBufferVec_free_storage(&self->destroyed_buffers);
		SemaphoreVec_free_storage(&self->recycled_semaphores);
		SemaphoreVec_free_storage(&self->destroyed_semaphores);
		cbhvec_deinit(&self->graphics_submissions);
		cbhvec_deinit(&self->compute_submissions);
		cbhvec_deinit(&self->transfer_submissions);
		/* vbo/ubo block lists: free backing storage (begin() recycled+cleared the
		 * blocks but kept the array; the old BufferBlockVec member destructor that
		 * freed it is gone now they are plain structs). */
		bufferblock_vec_free_storage(&self->vbo_blocks);
		bufferblock_vec_free_storage(&self->ubo_blocks);
	}

	uint32_t Device::find_memory_type(BufferDomain domain, uint32_t mask)
	{
		uint32_t desired = 0, fallback = 0;
		switch (domain)
		{
			case BufferDomain_Device:
				desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				fallback = 0;
				break;

			case BufferDomain_Host:
				desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
				fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
				break;

			case BufferDomain_CachedHost:
				desired = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
				fallback = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
				break;
		}

		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
		{
			if ((1u << i) & mask)
			{
				uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
				if ((flags & desired) == desired)
					return i;
			}
		}

		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
		{
			if ((1u << i) & mask)
			{
				uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
				if ((flags & fallback) == fallback)
					return i;
			}
		}

		LOGE("Couldn't find memory type for buffer domain.\n");
		return UINT32_MAX;
	}

	uint32_t Device::find_memory_type(ImageDomain domain, uint32_t mask)
	{
		uint32_t desired = 0, fallback = 0;
		switch (domain)
		{
			case ImageDomain_Physical:
				desired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				fallback = 0;
				break;

			case ImageDomain_Transient:
				desired = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
				fallback = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
				break;
		}

		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
		{
			if ((1u << i) & mask)
			{
				uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
				if ((flags & desired) == desired)
					return i;
			}
		}

		for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
		{
			if ((1u << i) & mask)
			{
				uint32_t flags = mem_props.memoryTypes[i].propertyFlags;
				if ((flags & fallback) == fallback)
					return i;
			}
		}

		LOGE("Couldn't find memory type for image domain.\n");
		return UINT32_MAX;
	}

	static inline VkImageViewType get_image_view_type(const ImageCreateInfo &create_info, const ImageViewCreateInfo *view)
	{
		unsigned layers = view ? view->layers : create_info.layers;
		unsigned base_layer = view ? view->base_layer : 0;

		if (layers == VK_REMAINING_ARRAY_LAYERS)
			layers = create_info.layers - base_layer;

		switch (create_info.type)
		{
			case VK_IMAGE_TYPE_1D:
				VK_ASSERT(create_info.width >= 1);
				VK_ASSERT(create_info.height == 1);
				VK_ASSERT(create_info.depth == 1);
				VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

				if (layers > 1)
					return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
				else
					return VK_IMAGE_VIEW_TYPE_1D;

			case VK_IMAGE_TYPE_2D:
				VK_ASSERT(create_info.width >= 1);
				VK_ASSERT(create_info.height >= 1);
				VK_ASSERT(create_info.depth == 1);

				if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
				{
					VK_ASSERT(create_info.width == create_info.height);

					if (layers > 6)
						return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
					else
						return VK_IMAGE_VIEW_TYPE_CUBE;
				}
				else
				{
					if (layers > 1)
						return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
					else
						return VK_IMAGE_VIEW_TYPE_2D;
				}

			case VK_IMAGE_TYPE_3D:
				VK_ASSERT(create_info.width >= 1);
				VK_ASSERT(create_info.height >= 1);
				VK_ASSERT(create_info.depth >= 1);
				return VK_IMAGE_VIEW_TYPE_3D;

			default:
				VK_ASSERT(0 && "bogus");
				return VK_IMAGE_VIEW_TYPE_RANGE_SIZE;
		}
	}

	BufferViewHandle Device::create_buffer_view(const BufferViewCreateInfo &view_info)
	{
		VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		info.buffer = buffer_get_buffer(view_info.buffer);
		info.format = view_info.format;
		info.offset = view_info.offset;
		info.range = view_info.range;

		VkBufferView view;
		VkResult res = vkCreateBufferView(device, &info, NULL, &view);
		if (res != VK_SUCCESS)
			return bvh_make(NULL);

		{ struct BufferView *_bv = (struct BufferView *)object_pool_raw_allocate(&handle_pool.buffer_views); bufferview_init(_bv, this, view, view_info); return bvh_make(_bv); }
	}

	/* ImageResourceHolder: a scoped owner of the Vulkan objects created while
	 * building an Image (the image, its memory/allocation, and the various image
	 * views). Converted from a C++ RAII class to a plain C struct +
	 * image_resource_holder_* free functions. The destructor (cleanup() if still
	 * owned) becomes image_resource_holder_fini, which the two using functions
	 * (create_image_view / create_image) call explicitly before every return.
	 * Ownership is released by setting owned=false once the objects are handed off. */
	struct ImageResourceHolder
	{
		VkDevice device;

		VkImage image;
		VkDeviceMemory memory;
		VkImageView image_view;
		VkImageView depth_view;
		VkImageView stencil_view;
		RenderTargetViewVec rt_views;
		DeviceAllocation allocation;
		DeviceAllocator *allocator;
		bool owned;
	};

	static inline void image_resource_holder_init(struct ImageResourceHolder *self, VkDevice device)
	{
		self->device = device;
		self->image = VK_NULL_HANDLE;
		self->memory = VK_NULL_HANDLE;
		self->image_view = VK_NULL_HANDLE;
		self->depth_view = VK_NULL_HANDLE;
		self->stencil_view = VK_NULL_HANDLE;
		self->rt_views.items = NULL; self->rt_views.count = 0; self->rt_views.cap = 0;
		memset(&self->allocation, 0, sizeof(self->allocation));
		self->allocator = NULL;
		self->owned = true;
	}

	static void image_resource_holder_cleanup(struct ImageResourceHolder *self);
	static inline void image_resource_holder_fini(struct ImageResourceHolder *self)
	{
		if (self->owned)
			image_resource_holder_cleanup(self);
	}

	bool image_resource_holder_create_default_views(struct ImageResourceHolder *self, const ImageCreateInfo &create_info, const VkImageViewCreateInfo *view_info);
	static bool image_resource_holder_create_render_target_views(struct ImageResourceHolder *self, const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info);
	static bool image_resource_holder_create_alt_views(struct ImageResourceHolder *self, const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info);
	static bool image_resource_holder_create_default_view(struct ImageResourceHolder *self, const VkImageViewCreateInfo &info);

	bool image_resource_holder_create_default_views(struct ImageResourceHolder *self, const ImageCreateInfo &create_info, const VkImageViewCreateInfo *view_info)
	{
		if ((create_info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
						VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) == 0)
		{
			LOGE("Cannot create image view unless certain usage flags are present.\n");
			return false;
		}

		VkImageViewCreateInfo default_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		if (!view_info)
		{
			default_view_info.image = self->image;
			default_view_info.format = create_info.format;
			default_view_info.components = create_info.swizzle;
			default_view_info.subresourceRange.aspectMask = format_to_aspect_mask(default_view_info.format);
			default_view_info.viewType = get_image_view_type(create_info, NULL);
			default_view_info.subresourceRange.baseMipLevel = 0;
			default_view_info.subresourceRange.baseArrayLayer = 0;
			default_view_info.subresourceRange.levelCount = create_info.levels;
			default_view_info.subresourceRange.layerCount = create_info.layers;
			view_info = &default_view_info;
		}

		if (!image_resource_holder_create_alt_views(self, create_info, *view_info))
			return false;

		if (!image_resource_holder_create_render_target_views(self, create_info, *view_info))
			return false;

		if (!image_resource_holder_create_default_view(self, *view_info))
			return false;

		return true;
	}

	static bool image_resource_holder_create_render_target_views(struct ImageResourceHolder *self, const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{

		if (info.viewType == VK_IMAGE_VIEW_TYPE_3D)
			return true;

		// If we have a render target, and non-trivial case (layers = 1, levels = 1),
		// create an array of render targets which correspond to each layer (mip 0).
		if ((image_create_info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0 &&
				((info.subresourceRange.levelCount > 1) || (info.subresourceRange.layerCount > 1)))
		{
			VkImageViewCreateInfo view_info = info;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
			view_info.subresourceRange.baseMipLevel = info.subresourceRange.baseMipLevel;
			for (uint32_t layer = 0; layer < info.subresourceRange.layerCount; layer++)
			{
				view_info.subresourceRange.levelCount = 1;
				view_info.subresourceRange.layerCount = 1;
				view_info.subresourceRange.baseArrayLayer = layer + info.subresourceRange.baseArrayLayer;

				VkImageView rt_view;
				if (vkCreateImageView(self->device, &view_info, NULL, &rt_view) != VK_SUCCESS)
					return false;

				RenderTargetViewVec_push(&self->rt_views, &rt_view);
			}
		}

		return true;
	}

	static bool image_resource_holder_create_alt_views(struct ImageResourceHolder *self, const ImageCreateInfo &image_create_info, const VkImageViewCreateInfo &info)
	{
		if (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
				info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
				info.viewType == VK_IMAGE_VIEW_TYPE_3D)
		{
			return true;
		}

		if (info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			if ((image_create_info.usage & ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				// Sanity check. Don't want to implement layered views for this.
				if (info.subresourceRange.levelCount > 1)
				{
					LOGE("Cannot create depth stencil attachments with more than 1 mip level currently, and non-DS usage flags.\n");
					return false;
				}

				if (info.subresourceRange.layerCount > 1)
				{
					LOGE("Cannot create layered depth stencil attachments with non-DS usage flags.\n");
					return false;
				}

				VkImageViewCreateInfo view_info = info;

				// We need this to be able to sample the texture, or otherwise use it as a non-pure DS attachment.
				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (vkCreateImageView(self->device, &view_info, NULL, &self->depth_view) != VK_SUCCESS)
					return false;

				view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
				if (vkCreateImageView(self->device, &view_info, NULL, &self->stencil_view) != VK_SUCCESS)
					return false;
			}
		}

		return true;
	}

	static bool image_resource_holder_create_default_view(struct ImageResourceHolder *self, const VkImageViewCreateInfo &info)
	{
		// Create the normal image view. This one contains every subresource.
		if (vkCreateImageView(self->device, &info, NULL, &self->image_view) != VK_SUCCESS)
			return false;

		return true;
	}

	static void image_resource_holder_cleanup(struct ImageResourceHolder *self)
	{
		if (self->image_view)
			vkDestroyImageView(self->device, self->image_view, NULL);
		if (self->depth_view)
			vkDestroyImageView(self->device, self->depth_view, NULL);
		if (self->stencil_view)
			vkDestroyImageView(self->device, self->stencil_view, NULL);
		{
			int _i;
			for (_i = 0; _i < RenderTargetViewVec_size(&self->rt_views); _i++)
				vkDestroyImageView(self->device, *RenderTargetViewVec_at(&self->rt_views, _i), NULL);
		}
		RenderTargetViewVec_free_storage(&self->rt_views);

		if (self->image)
			vkDestroyImage(self->device, self->image, NULL);
		if (self->memory)
			vkFreeMemory(self->device, self->memory, NULL);
		if (self->allocator)
			deviceallocation_free_immediate_alloc(&self->allocation, self->allocator);
	}

	ImageViewHandle Device::create_image_view(const ImageViewCreateInfo &create_info)
	{
		ImageResourceHolder holder;
		image_resource_holder_init(&holder, device);
		const ImageCreateInfo &image_create_info = image_get_create_info(create_info.image);

		VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = image_get_image(create_info.image);
		view_info.format = format;
		view_info.components = create_info.swizzle;
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
		view_info.subresourceRange.baseMipLevel = create_info.base_level;
		view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
		view_info.subresourceRange.levelCount = create_info.levels;
		view_info.subresourceRange.layerCount = create_info.layers;
		view_info.viewType = get_image_view_type(image_create_info, &create_info);

		unsigned num_levels;
		if (view_info.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
			num_levels = image_get_create_info(create_info.image).levels - view_info.subresourceRange.baseMipLevel;
		else
			num_levels = view_info.subresourceRange.levelCount;

		unsigned num_layers;
		if (view_info.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS)
			num_layers = image_get_create_info(create_info.image).layers - view_info.subresourceRange.baseArrayLayer;
		else
			num_layers = view_info.subresourceRange.layerCount;

		view_info.subresourceRange.levelCount = num_levels;
		view_info.subresourceRange.layerCount = num_layers;

		if (!image_resource_holder_create_default_views(&holder, image_create_info, &view_info))
		{
			image_resource_holder_fini(&holder);
			return iv_make(NULL);
		}

		ImageViewCreateInfo tmp = create_info;
		tmp.format = format;
		struct ImageView *_iv = (struct ImageView *)object_pool_raw_allocate(&handle_pool.image_views); imageview_init(_iv, this, holder.image_view, tmp); ImageViewHandle ret = iv_make(_iv);
		if (iv_is_valid(&ret))
		{
			holder.owned = false;
			imageview_set_alt_views(iv_get(&ret), holder.depth_view, holder.stencil_view);
			imageview_set_render_target_views(iv_get(&ret), holder.rt_views);
			holder.rt_views.items = NULL; holder.rt_views.count = 0; holder.rt_views.cap = 0;
			image_resource_holder_fini(&holder);
			return ret;
		}
		else
		{
			image_resource_holder_fini(&holder);
			return iv_make(NULL);
		}
	}

	InitialImageBuffer Device::create_image_staging_buffer(const ImageCreateInfo &info, const ImageInitialData *initial)
	{
		InitialImageBuffer result;

		bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
		TextureFormatLayout layout;
		tfl_init(&layout);

		unsigned copy_levels;
		if (generate_mips)
			copy_levels = 1;
		else if (info.levels == 0)
			copy_levels = tfl_num_miplevels(info.width, info.height, info.depth);
		else
			copy_levels = info.levels;

		switch (info.type)
		{
			case VK_IMAGE_TYPE_1D:
				tfl_set_1d(&layout, info.format, info.width, info.layers, copy_levels);
				break;
			case VK_IMAGE_TYPE_2D:
				tfl_set_2d(&layout, info.format, info.width, info.height, info.layers, copy_levels);
				break;
			case VK_IMAGE_TYPE_3D:
				tfl_set_3d(&layout, info.format, info.width, info.height, info.depth, copy_levels);
				break;
			default:
				return {};
		}

		BufferCreateInfo buffer_info = {};
		buffer_info.domain = BufferDomain_Host;
		buffer_info.size = tfl_get_required_size(&layout);
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		result.buffer = create_buffer(buffer_info, NULL);
		set_name(*bh_get(&result.buffer), "image-upload-staging-buffer");

		// And now, do the actual copy.
		uint8_t *mapped = (uint8_t *)(map_host_buffer(*bh_get(&result.buffer), MEMORY_ACCESS_WRITE_BIT));
		unsigned index = 0;

		tfl_set_buffer(&layout, mapped, tfl_get_required_size(&layout));

		for (unsigned level = 0; level < copy_levels; level++)
		{
			const TextureFormatLayoutMipInfo *mip_info = tfl_get_mip_info(&layout, level);
			uint32_t dst_height_stride = tfl_get_layer_size(&layout, level);
			size_t row_size = tfl_get_row_size(&layout, level);

			for (unsigned layer = 0; layer < info.layers; layer++, index++)
			{
				uint32_t src_row_length =
					initial[index].row_length ? initial[index].row_length : mip_info->row_length;
				uint32_t src_array_height =
					initial[index].image_height ? initial[index].image_height : mip_info->image_height;

				uint32_t src_row_stride = tfl_row_byte_stride(&layout, src_row_length);
				uint32_t src_height_stride = tfl_layer_byte_stride(&layout, src_array_height, src_row_stride);

				uint8_t *dst = (uint8_t *)(tfl_data(&layout, layer, level));
				const uint8_t *src = (const uint8_t *)(initial[index].data);

				for (uint32_t z = 0; z < mip_info->depth; z++)
					for (uint32_t y = 0; y < mip_info->block_image_height; y++)
						memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
			}
		}

		unmap_host_buffer(*bh_get(&result.buffer), MEMORY_ACCESS_WRITE_BIT);
		tfl_build_buffer_image_copies(&layout, result.blits, &result.num_blits);
		return result;
	}

	ImageHandle Device::create_image(const ImageCreateInfo &create_info, const ImageInitialData *initial)
	{
		if (initial)
		{
			InitialImageBuffer staging_buffer = create_image_staging_buffer(create_info, initial);
			ImageHandle img = create_image_from_staging_buffer(create_info, &staging_buffer);
			/* Drop the staging buffer's reference (previously released by the
			 * InitialImageBuffer destructor at end of scope). */
			bh_reset(&staging_buffer.buffer);
			return img;
		}
		else
			return create_image_from_staging_buffer(create_info, NULL);
	}

	ImageHandle Device::create_image_from_staging_buffer(const ImageCreateInfo &create_info,
			const InitialImageBuffer *staging_buffer)
	{
		ImageResourceHolder holder;
		VkMemoryRequirements reqs;
		image_resource_holder_init(&holder, device);

		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.format = create_info.format;
		info.extent.width = create_info.width;
		info.extent.height = create_info.height;
		info.extent.depth = create_info.depth;
		info.imageType = create_info.type;
		info.mipLevels = create_info.levels;
		info.arrayLayers = create_info.layers;
		info.samples = create_info.samples;

		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		info.usage = create_info.usage;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (create_info.domain == ImageDomain_Transient)
			info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		if (staging_buffer)
			info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		info.flags = create_info.flags;

		if (info.mipLevels == 0)
			info.mipLevels = image_num_miplevels(info.extent);

		if (create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT)
			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

		if (!image_format_is_supported(create_info.format, image_usage_to_features(info.usage), info.tiling))
		{
			LOGE("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
			image_resource_holder_fini(&holder);
			return ih_make(NULL);
		}

		if (vkCreateImage(device, &info, NULL, &holder.image) != VK_SUCCESS)
		{
			LOGE("Failed to create image in vkCreateImage.\n");
			image_resource_holder_fini(&holder);
			return ih_make(NULL);
		}

		vkGetImageMemoryRequirements(device, holder.image, &reqs);
		uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
		if (memory_type == UINT32_MAX)
		{
			image_resource_holder_fini(&holder);
			return ih_make(NULL);
		}

		if (!deviceallocator_allocate_image_memory(&managers.memory, reqs.size, reqs.alignment, memory_type,
					ALLOCATION_TILING_OPTIMAL,
					&holder.allocation, holder.image))
		{
			LOGE("Failed to allocate image memory (type %u, size: %u).\n", unsigned(memory_type), unsigned(reqs.size));
			image_resource_holder_fini(&holder);
			return ih_make(NULL);
		}

		if (vkBindImageMemory(device, holder.image, deviceallocation_get_memory(&holder.allocation), deviceallocation_get_offset(&holder.allocation)) != VK_SUCCESS)
		{
			LOGE("Failed to bind image memory.\n");
			image_resource_holder_fini(&holder);
			return ih_make(NULL);
		}

		ImageCreateInfo tmpinfo = create_info;
		tmpinfo.usage = info.usage;
		tmpinfo.levels = info.mipLevels;

		bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;
		if (has_view)
		{
			if (!image_resource_holder_create_default_views(&holder, tmpinfo, NULL))
			{
				image_resource_holder_fini(&holder);
				return ih_make(NULL);
			}
		}

		struct Image *_im = (struct Image *)object_pool_raw_allocate(&handle_pool.images); image_init(_im, this, holder.image, holder.image_view, holder.allocation, tmpinfo); ImageHandle handle = ih_make(_im);
		if (ih_is_valid(&handle))
		{
			holder.owned = false;
			if (has_view)
			{
				imageview_set_alt_views(&image_get_view(ih_get(&handle)), holder.depth_view, holder.stencil_view);
				imageview_set_render_target_views(&image_get_view(ih_get(&handle)), holder.rt_views);
				holder.rt_views.items = NULL; holder.rt_views.count = 0; holder.rt_views.cap = 0;
			}

			// Set possible dstStage and dstAccess.
			image_set_stage_flags(ih_get(&handle), image_usage_to_possible_stages(info.usage));
			image_set_access_flags(ih_get(&handle), image_usage_to_possible_access(info.usage));
		}

		// Copy initial data to texture.
		if (staging_buffer)
		{
			VK_ASSERT(create_info.domain != ImageDomain_Transient);
			VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
			bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

			// If graphics_queue != transfer_queue, we will use a semaphore, so no srcAccess mask is necessary.
			VkAccessFlags final_transition_src_access = 0;
			if (generate_mips)
				final_transition_src_access = VK_ACCESS_TRANSFER_READ_BIT; // Validation complains otherwise.
			else if (graphics_queue == transfer_queue)
				final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

			VkAccessFlags prepare_src_access = graphics_queue == transfer_queue ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;
			bool need_mipmap_barrier = true;
			bool need_initial_barrier = true;

			// Now we've used the TRANSFER queue to copy data over to the GPU.
			// For mipmapping, we're now moving over to graphics,
			// the transfer queue is designed for CPU <-> GPU and that's it.

			// For concurrent queue mode, we just need to inject a semaphore.
			// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

			CommandBufferHandle graphics_cmd = request_command_buffer(Type_Generic);
			CommandBufferHandle transfer_cmd; transfer_cmd.data = NULL;

			// Don't split the upload into multiple command buffers unless we have to.
			if (transfer_queue != graphics_queue)
				transfer_cmd = request_command_buffer(Type_AsyncTransfer);
			else
				transfer_cmd = graphics_cmd;

			commandbuffer_image_barrier(cbh_get(&transfer_cmd), *ih_get(&handle), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT);

			commandbuffer_begin_region(cbh_get(&transfer_cmd), "copy-image-to-gpu");
			commandbuffer_copy_buffer_to_image_blits(cbh_get(&transfer_cmd), *ih_get(&handle), *bh_get(&staging_buffer->buffer), staging_buffer->num_blits, staging_buffer->blits);
			commandbuffer_end_region(cbh_get(&transfer_cmd));

			if (transfer_queue != graphics_queue)
			{
				VkPipelineStageFlags dst_stages =
					generate_mips ? VkPipelineStageFlags(VK_PIPELINE_STAGE_TRANSFER_BIT) : image_get_stage_flags(ih_get(&handle));

				// We can't just use semaphores, we will also need a release + acquire barrier to marshal ownership from
				// transfer queue over to graphics ...
				if (transfer_queue_family_index != graphics_queue_family_index)
				{
					need_mipmap_barrier = false;

					VkImageMemoryBarrier release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
					release.image = image_get_image(ih_get(&handle));
					release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					release.dstAccessMask = 0;
					release.srcQueueFamilyIndex = transfer_queue_family_index;
					release.dstQueueFamilyIndex = graphics_queue_family_index;
					release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

					if (generate_mips)
					{
						release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
						release.subresourceRange.levelCount = 1;
					}
					else
					{
						release.newLayout = create_info.initial_layout;
						release.subresourceRange.levelCount = info.mipLevels;
						need_initial_barrier = false;
					}

					release.subresourceRange.aspectMask = format_to_aspect_mask(info.format);
					release.subresourceRange.layerCount = info.arrayLayers;

					VkImageMemoryBarrier acquire = release;
					acquire.srcAccessMask = 0;

					if (generate_mips)
						acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					else
						acquire.dstAccessMask = image_get_access_flags(ih_get(&handle)) & image_layout_to_possible_access(create_info.initial_layout);

					commandbuffer_barrier(cbh_get(&transfer_cmd), VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
							0, NULL, 0, NULL, 1, &release);

					commandbuffer_barrier(cbh_get(&graphics_cmd), dst_stages,
							dst_stages,
							0, NULL, 0, NULL, 1, &acquire);
				}

				Semaphore sem; sem.data = NULL;
				submit(transfer_cmd, NULL, 1, &sem);
				add_wait_semaphore_nolock(Type_Generic, sem, dst_stages, true);
				sem_reset(&sem);
			}

			if (generate_mips)
			{
				commandbuffer_begin_region(cbh_get(&graphics_cmd), "mipgen");
				commandbuffer_barrier_prepare_generate_mipmap(cbh_get(&graphics_cmd), *ih_get(&handle), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						prepare_src_access, need_mipmap_barrier);
				commandbuffer_generate_mipmap(cbh_get(&graphics_cmd), *ih_get(&handle));
				commandbuffer_end_region(cbh_get(&graphics_cmd));
			}

			if (need_initial_barrier)
			{
				commandbuffer_image_barrier(cbh_get(&graphics_cmd), 
						*ih_get(&handle), generate_mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						create_info.initial_layout,
						VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
						image_get_stage_flags(ih_get(&handle)),
						image_get_access_flags(ih_get(&handle)) & image_layout_to_possible_access(create_info.initial_layout));
			}

			bool share_async_graphics = get_physical_queue_type(Type_AsyncGraphics) == Type_AsyncCompute;

			// Add semaphore if the compute queue can be used for async graphics as well.
			if (share_async_graphics)
			{
				Semaphore sem; sem.data = NULL;
				submit(graphics_cmd, NULL, 1, &sem);

				VkPipelineStageFlags dst_stages = image_get_stage_flags(ih_get(&handle));
				if (graphics_queue_family_index != compute_queue_family_index)
					dst_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
				add_wait_semaphore_nolock(Type_AsyncCompute, sem, dst_stages, true);
				sem_reset(&sem);
			}
			else
				submit(graphics_cmd);
		}
		else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
		{
			VK_ASSERT(create_info.domain != ImageDomain_Transient);
			CommandBufferHandle cmd = request_command_buffer(Type_Generic);
			commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&handle), info.initialLayout, create_info.initial_layout,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, image_get_stage_flags(ih_get(&handle)),
					image_get_access_flags(ih_get(&handle)) &
					image_layout_to_possible_access(create_info.initial_layout));

			submit(cmd);
		}

		return handle;
	}

	static VkSamplerCreateInfo fill_vk_sampler_info(const SamplerCreateInfo &sampler_info)
	{
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		info.magFilter = sampler_info.mag_filter;
		info.minFilter = sampler_info.min_filter;
		info.mipmapMode = sampler_info.mipmap_mode;
		info.addressModeU = sampler_info.address_mode_u;
		info.addressModeV = sampler_info.address_mode_v;
		info.addressModeW = sampler_info.address_mode_w;
		info.mipLodBias = sampler_info.mip_lod_bias;
		info.anisotropyEnable = sampler_info.anisotropy_enable;
		info.maxAnisotropy = sampler_info.max_anisotropy;
		info.compareEnable = sampler_info.compare_enable;
		info.compareOp = sampler_info.compare_op;
		info.minLod = sampler_info.min_lod;
		info.maxLod = sampler_info.max_lod;
		info.borderColor = sampler_info.border_color;
		info.unnormalizedCoordinates = sampler_info.unnormalized_coordinates;
		return info;
	}

	SamplerHandle Device::create_sampler(const SamplerCreateInfo &sampler_info, StockSampler stock_sampler)
	{
		VkSamplerCreateInfo info = fill_vk_sampler_info(sampler_info);
		VkSampler sampler;

		(void)stock_sampler;
		if (vkCreateSampler(device, &info, NULL, &sampler) != VK_SUCCESS)
			return smh_make(NULL);

		{ struct Sampler *_s = (struct Sampler *)object_pool_raw_allocate(&handle_pool.samplers); sampler_init(_s, this, sampler); return smh_make(_s); }
	}

	BufferHandle Device::create_buffer(const BufferCreateInfo &create_info, const void *initial)
	{
		VkBuffer buffer;
		VkMemoryRequirements reqs;
		DeviceAllocation allocation;

		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = create_info.size;
		info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		uint32_t sharing_indices[3];
		if (graphics_queue_family_index != compute_queue_family_index ||
				graphics_queue_family_index != transfer_queue_family_index)
		{
			// For buffers, always just use CONCURRENT access modes,
			// so we don't have to deal with acquire/release barriers in async compute.
			info.sharingMode = VK_SHARING_MODE_CONCURRENT;

			sharing_indices[info.queueFamilyIndexCount++] = graphics_queue_family_index;

			if (graphics_queue_family_index != compute_queue_family_index)
				sharing_indices[info.queueFamilyIndexCount++] = compute_queue_family_index;

			if (graphics_queue_family_index != transfer_queue_family_index &&
					compute_queue_family_index != transfer_queue_family_index)
			{
				sharing_indices[info.queueFamilyIndexCount++] = transfer_queue_family_index;
			}

			info.pQueueFamilyIndices = sharing_indices;
		}

		if (vkCreateBuffer(device, &info, NULL, &buffer) != VK_SUCCESS)
			return bh_make(NULL);

		vkGetBufferMemoryRequirements(device, buffer, &reqs);

		uint32_t memory_type = find_memory_type(create_info.domain, reqs.memoryTypeBits);
		if (memory_type == UINT32_MAX)
		{
			vkDestroyBuffer(device, buffer, NULL);
			return bh_make(NULL);
		}

		if (!deviceallocator_allocate_typed(&managers.memory, reqs.size, reqs.alignment, memory_type, ALLOCATION_TILING_LINEAR, &allocation))
		{
			vkDestroyBuffer(device, buffer, NULL);
			return bh_make(NULL);
		}

		if (vkBindBufferMemory(device, buffer, deviceallocation_get_memory(&allocation), deviceallocation_get_offset(&allocation)) != VK_SUCCESS)
		{
			deviceallocation_free_immediate_alloc(&allocation, &managers.memory);
			vkDestroyBuffer(device, buffer, NULL);
			return bh_make(NULL);
		}

		BufferCreateInfo tmpinfo = create_info;
		tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		struct Buffer *_b = (struct Buffer *)object_pool_raw_allocate(&handle_pool.buffers); buffer_init(_b, this, buffer, allocation, tmpinfo); BufferHandle handle = bh_make(_b);

		if (create_info.domain == BufferDomain_Device && initial && !memory_type_is_host_visible(memory_type))
		{
			CommandBufferHandle cmd; cmd.data = NULL;
			BufferCreateInfo staging_info = create_info;
			staging_info.domain = BufferDomain_Host;
			BufferHandle staging_buffer = create_buffer(staging_info, initial);
			set_name(*bh_get(&staging_buffer), "buffer-upload-staging-buffer");

			cmd = request_command_buffer(Type_AsyncTransfer);
			commandbuffer_begin_region(cbh_get(&cmd), "copy-buffer-staging");
			commandbuffer_copy_buffer_whole(cbh_get(&cmd), *bh_get(&handle), *bh_get(&staging_buffer));
			commandbuffer_end_region(cbh_get(&cmd));

			submit_staging(cmd, info.usage, true);
			/* Drop the staging buffer's producer reference (the GPU copy is
			 * submitted; the staging buffer's lifetime ends with this scope,
			 * previously via the handle's destructor). */
			bh_reset(&staging_buffer);
		}
		else if (initial)
		{
			void *ptr = deviceallocator_map_memory(&managers.memory, &allocation, MEMORY_ACCESS_WRITE_BIT);
			if (!ptr)
				return bh_make(NULL);

			memcpy(ptr, initial, create_info.size);
			deviceallocator_unmap_memory(&managers.memory, &allocation, MEMORY_ACCESS_WRITE_BIT);
		}
		return handle;
	}

	bool Device::get_image_format_properties(VkFormat format, VkImageType type, VkImageTiling tiling,
			VkImageUsageFlags usage, VkImageCreateFlags flags,
			VkImageFormatProperties *properties)
	{
		VkResult res = vkGetPhysicalDeviceImageFormatProperties(gpu, format, type, tiling, usage, flags,
				properties);
		return res == VK_SUCCESS;
	}

	bool Device::image_format_is_supported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling) const
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
		VkFormatFeatureFlags flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props.optimalTilingFeatures : props.linearTilingFeatures;
		return (flags & required) == required;
	}

	VkFormat Device::get_default_depth_format() const
	{
		if (image_format_is_supported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D32_SFLOAT;
		if (image_format_is_supported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_X8_D24_UNORM_PACK32;
		if (image_format_is_supported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D16_UNORM;

		return VK_FORMAT_UNDEFINED;
	}

	const RenderPass &Device::request_render_pass(const RenderPassInfo &info, bool compatible)
	{
		Hasher h; hasher_init(&h);
		VkFormat formats[VULKAN_NUM_ATTACHMENTS];
		VkFormat depth_stencil;
		uint32_t lazy = 0;
		uint32_t optimal = 0;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			formats[i] = imageview_get_format(info.color_attachments[i]);
			if (image_get_create_info(&imageview_get_image(info.color_attachments[i])).domain == ImageDomain_Transient)
				lazy |= 1u << i;
			if (image_get_layout_type(&imageview_get_image(info.color_attachments[i])) == Layout_Optimal)
				optimal |= 1u << i;
		}

		if (info.depth_stencil)
		{
			if (image_get_create_info(&imageview_get_image(info.depth_stencil)).domain == ImageDomain_Transient)
				lazy |= 1u << info.num_color_attachments;
			if (image_get_layout_type(&imageview_get_image(info.depth_stencil)) == Layout_Optimal)
				optimal |= 1u << info.num_color_attachments;
		}

		hasher_u32(&h, info.num_subpasses);
		for (unsigned i = 0; i < info.num_subpasses; i++)
		{
			hasher_u32(&h, info.subpasses[i].num_color_attachments);
			hasher_u32(&h, info.subpasses[i].num_input_attachments);
			hasher_u32(&h, info.subpasses[i].num_resolve_attachments);
			hasher_u32(&h, (uint32_t)(info.subpasses[i].depth_stencil_mode));
			for (unsigned j = 0; j < info.subpasses[i].num_color_attachments; j++)
				hasher_u32(&h, info.subpasses[i].color_attachments[j]);
			for (unsigned j = 0; j < info.subpasses[i].num_input_attachments; j++)
				hasher_u32(&h, info.subpasses[i].input_attachments[j]);
			for (unsigned j = 0; j < info.subpasses[i].num_resolve_attachments; j++)
				hasher_u32(&h, info.subpasses[i].resolve_attachments[j]);
		}

		depth_stencil = info.depth_stencil ? imageview_get_format(info.depth_stencil) : VK_FORMAT_UNDEFINED;
		hasher_data(&h, (const uint32_t *)(formats), info.num_color_attachments * sizeof(VkFormat));
		hasher_u32(&h, info.num_color_attachments);
		hasher_u32(&h, depth_stencil);

		// Compatible render passes do not care about load/store, or image layouts.
		if (!compatible)
		{
			hasher_u32(&h, info.op_flags);
			hasher_u32(&h, info.clear_attachments);
			hasher_u32(&h, info.load_attachments);
			hasher_u32(&h, info.store_attachments);
			hasher_u32(&h, optimal);
		}

		// Lazy flag can change external subpass dependencies, which is not compatible.
		hasher_u32(&h, lazy);

		Hash hash = hasher_get(&h);

		RenderPass *ret = render_pass_map_find(&render_passes, hash);
		if (!ret)
			ret = render_pass_map_emplace_yield(&render_passes, hash, this, info);
		return *ret;
	}

	void Device::set_name(const Buffer &buffer, const char *name)
	{
		if (ext.supports_debug_marker)
		{
			VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
			info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
			info.object = (uint64_t)buffer_get_buffer(&buffer);
			info.pObjectName = name;
			vkDebugMarkerSetObjectNameEXT(device, &info);
		}
	}

	void Device::set_name(const Image &image, const char *name)
	{
		if (ext.supports_debug_marker)
		{
			VkDebugMarkerObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT };
			info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
			info.object = (uint64_t)image_get_image(&image);
			info.pObjectName = name;
			vkDebugMarkerSetObjectNameEXT(device, &info);
		}
	}


/* === atlas.cpp === */



	static void fbatlas_load_image(FBAtlas *self, const Rect &rect)
	{
		if (rect.width == 0 || rect.height == 0)
			return;

		fbatlas_write_compute(self, Domain_Unscaled, rect);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				(*fbatlas_info(self, x, y)) &= ~STATUS_TEXTURE_RENDERED;
	}

	static bool fbatlas_texture_rendered(FBAtlas *self, const Rect &rect)
	{
		if (rect.width == 0 || rect.height == 0)
			return false;

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				if ((*fbatlas_info(self, x, y)) & STATUS_TEXTURE_RENDERED)
					return true;
		return false;
	}

	static Domain fbatlas_blit_vram(FBAtlas *self, const Rect &dst, const Rect &src)
	{
		Domain domain = fbatlas_find_suitable_domain(self, src);

		fbatlas_sync_domain(self, domain, src);
		fbatlas_sync_domain(self, domain, dst);
		fbatlas_read_domain(self, domain, Stage_Compute, src);
		fbatlas_write_domain(self, domain, Stage_Compute, dst);

		unsigned dst_xbegin = dst.x / BLOCK_WIDTH;
		unsigned dst_xend = (dst.x + dst.width - 1) / BLOCK_WIDTH;
		unsigned dst_ybegin = dst.y / BLOCK_HEIGHT;
		unsigned dst_yend = (dst.y + dst.height - 1) / BLOCK_HEIGHT;

		unsigned src_xbegin = src.x / BLOCK_WIDTH;
		unsigned src_xend = (src.x + src.width - 1) / BLOCK_WIDTH;
		unsigned src_ybegin = src.y / BLOCK_HEIGHT;
		unsigned src_yend = (src.y + src.height - 1) / BLOCK_HEIGHT;

		{
			unsigned j_max = (dst_yend - dst_ybegin) < (src_yend - src_ybegin)
				? (dst_yend - dst_ybegin) : (src_yend - src_ybegin);
			unsigned i_max = (dst_xend - dst_xbegin) < (src_xend - src_xbegin)
				? (dst_xend - dst_xbegin) : (src_xend - src_xbegin);
			for (unsigned j = 0; j <= j_max; j++)
				for (unsigned i = 0; i <= i_max; i++)
				{
					bool rendered = (*fbatlas_info(self, src_xbegin + i, src_ybegin + j)) & STATUS_TEXTURE_RENDERED;
					if (rendered)
						(*fbatlas_info(self, dst_xbegin + i, dst_ybegin + j)) |= STATUS_TEXTURE_RENDERED;
					else
						(*fbatlas_info(self, dst_xbegin + i, dst_ybegin + j)) &= ~STATUS_TEXTURE_RENDERED;
				}
		}

		return domain;
	}

	static void fbatlas_read_fragment(FBAtlas *self, Domain domain, const Rect &rect)
	{
		fbatlas_sync_domain(self, domain, rect);
		fbatlas_read_domain(self, domain, Stage_Fragment, rect);
	}

	static void fbatlas_read_texture(FBAtlas *self, Domain domain)
	{
		Rect shifted = self->renderpass.texture_window;
		bool palette;
		switch (self->renderpass.texture_mode)
		{
			case TextureMode_Palette4bpp:
			case TextureMode_Palette8bpp:
				palette = true;
				break;

			default:
				palette = false;
				break;
		}
		shifted.x += self->renderpass.texture_offset_x;
		shifted.y += self->renderpass.texture_offset_y;

		//Domain domain = palette ? Domain_Unscaled : fbatlas_find_suitable_domain(self, shifted);
		fbatlas_sync_domain(self, domain, shifted);

		Rect palette_rect = { self->renderpass.palette_offset_x, self->renderpass.palette_offset_y,
			self->renderpass.texture_mode == TextureMode_Palette8bpp ? 256u : 16u, 1 };

		if (palette)
			fbatlas_sync_domain(self, domain, palette_rect);

		fbatlas_read_domain(self, domain, Stage_FragmentTexture, shifted);
		if (palette)
			fbatlas_read_domain(self, domain, Stage_FragmentTexture, palette_rect);
	}

	static bool fbatlas_write_domain(FBAtlas *self, Domain domain, Stage stage, const Rect &rect)
	{
		if (fbatlas_inside_render_pass(self, rect))
			fbatlas_flush_render_pass(self);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

		unsigned write_domains = 0;
		unsigned hazard_domains = 0;
		unsigned resolve_domains = 0;
		if (domain == Domain_Unscaled)
		{
			hazard_domains = STATUS_FB_WRITE | STATUS_FB_READ | STATUS_TEXTURE_READ;
			if (stage == Stage_Compute)
				resolve_domains = STATUS_COMPUTE_FB_WRITE;
			else if (stage == Stage_Transfer)
				resolve_domains = STATUS_TRANSFER_FB_WRITE;
			else if (stage == Stage_Fragment)
			{
				// Write-after-write in fragment is handled implicitly.
				// Write-after-read means rendering to a block after reading it as a texture.
				// This is a hazard we must handle.
				hazard_domains &= ~STATUS_FRAGMENT_FB_WRITE;
				resolve_domains = STATUS_FRAGMENT_FB_WRITE;
			}
			resolve_domains |= STATUS_FB_ONLY;
		}
		else
		{
			hazard_domains = STATUS_SFB_WRITE | STATUS_SFB_READ | STATUS_TEXTURE_READ;
			if (stage == Stage_Compute)
				resolve_domains = STATUS_COMPUTE_SFB_WRITE;
			else if (stage == Stage_Fragment)
			{
				// Write-after-write in fragment is handled implicitly.
				// Write-after-read means rendering to a block after reading it as a texture.
				// This is a hazard we must handle.
				hazard_domains &= ~STATUS_FRAGMENT_SFB_WRITE;
				resolve_domains = STATUS_FRAGMENT_SFB_WRITE;
			}
			else if (stage == Stage_Transfer)
				resolve_domains = STATUS_TRANSFER_SFB_WRITE;
			resolve_domains |= STATUS_SFB_ONLY;
		}

		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				write_domains |= (*fbatlas_info(self, x, y)) & hazard_domains;

		// Trying to update VRAM before fragment is done reading it.
		// We could use copy-on-write here to avoid flushing, but this scenario is very rare.
		if (write_domains & STATUS_TEXTURE_READ)
			fbatlas_flush_render_pass(self);

		if (write_domains)
			fbatlas_pipeline_barrier(self, write_domains);

		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				(*fbatlas_info(self, x, y)) = ((*fbatlas_info(self, x, y)) & ~STATUS_OWNERSHIP_MASK) | resolve_domains;

		return (write_domains & STATUS_FRAGMENT_SFB_READ) != 0;
	}

	static void fbatlas_read_domain(FBAtlas *self, Domain domain, Stage stage, const Rect &rect)
	{
		if (fbatlas_inside_render_pass(self, rect))
			fbatlas_flush_render_pass(self);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

		unsigned write_domains = 0;
		unsigned hazard_domains = 0;
		unsigned resolve_domains = 0;
		if (domain == Domain_Unscaled)
		{
			hazard_domains = STATUS_FB_WRITE;
			if (stage == Stage_Compute)
				resolve_domains = STATUS_COMPUTE_FB_READ;
			else if (stage == Stage_Transfer)
				resolve_domains = STATUS_TRANSFER_FB_READ;
			else if (stage == Stage_Fragment)
			{
				hazard_domains &= ~STATUS_FRAGMENT_FB_READ;
				resolve_domains = STATUS_FRAGMENT_FB_READ;
			}
			else if (stage == Stage_FragmentTexture)
			{
				hazard_domains &= ~(STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ);
				resolve_domains = STATUS_FRAGMENT_FB_READ | STATUS_TEXTURE_READ;
			}
		}
		else
		{
			hazard_domains = STATUS_SFB_WRITE;
			if (stage == Stage_Compute)
				resolve_domains = STATUS_COMPUTE_SFB_READ;
			else if (stage == Stage_Transfer)
				resolve_domains = STATUS_TRANSFER_SFB_READ;
			else if (stage == Stage_Fragment)
			{
				hazard_domains &= ~STATUS_FRAGMENT_SFB_READ;
				resolve_domains = STATUS_FRAGMENT_SFB_READ;
			}
			else if (stage == Stage_FragmentTexture)
			{
				hazard_domains &= ~(STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ);
				resolve_domains = STATUS_FRAGMENT_SFB_READ | STATUS_TEXTURE_READ;
			}
		}

		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				write_domains |= (*fbatlas_info(self, x, y)) & hazard_domains;

		if (write_domains)
			fbatlas_pipeline_barrier(self, write_domains);

		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				(*fbatlas_info(self, x, y)) |= resolve_domains;
	}

	static void fbatlas_sync_domain(FBAtlas *self, Domain domain, const Rect &rect)
	{
		if (fbatlas_inside_render_pass(self, rect))
			fbatlas_flush_render_pass(self);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

		// If we need to see a "clean" version
		// of a framebuffer domain, we need to see
		// anything other than this flag.
		unsigned dirty_bits = 1u << (domain == Domain_Unscaled ? STATUS_SFB_ONLY : STATUS_FB_ONLY);
		unsigned bits = 0;

		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				bits |= 1u << ((*fbatlas_info(self, x, y)) & STATUS_OWNERSHIP_MASK);

		unsigned write_domains = 0;

		// We're asserting that a region is up to date, but it's
		// not, so we have to resolve it.
		if ((bits & dirty_bits) == 0)
			return;

		// For scaled domain,
		// we need to blit from unscaled domain to scaled.
		unsigned ownership;
		unsigned hazard_domains;
		unsigned resolve_domains;
		if (domain == Domain_Scaled)
		{
			ownership = STATUS_FB_ONLY;
			hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_SFB_READ;

			//resolve_domains = STATUS_TRANSFER_FB_READ | STATUS_FB_PREFER | STATUS_TRANSFER_SFB_WRITE;
			resolve_domains = STATUS_COMPUTE_FB_READ | STATUS_FB_PREFER | STATUS_COMPUTE_SFB_WRITE;
		}
		else
		{
			ownership = STATUS_SFB_ONLY;
			hazard_domains = STATUS_FB_WRITE | STATUS_SFB_WRITE | STATUS_FB_READ;

			//resolve_domains = STATUS_TRANSFER_SFB_READ | STATUS_SFB_PREFER | STATUS_TRANSFER_FB_WRITE;
			resolve_domains = STATUS_COMPUTE_SFB_READ | STATUS_SFB_PREFER | STATUS_COMPUTE_FB_WRITE;
		}

		for (unsigned y = ybegin; y <= yend; y++)
		{
			for (unsigned x = xbegin; x <= xend; x++)
			{
				StatusFlags *mask = fbatlas_info(self, x, y);
				// If our block isn't in the ownership class we want,
				// we need to read from one block and write to the other.
				// We might have to wait for writers on read,
				// and add hazard masks for our writes
				// so other readers can wait for us.
				if (((*mask) & STATUS_OWNERSHIP_MASK) == ownership)
					write_domains |= (*mask) & hazard_domains;
			}
		}

		// If we hit any hazard, resolve it.
		if (write_domains)
			fbatlas_pipeline_barrier(self, write_domains);

		for (unsigned y = ybegin; y <= yend; y++)
		{
			for (unsigned x = xbegin; x <= xend; x++)
			{
				StatusFlags *mask = fbatlas_info(self, x, y);
				if (((*mask) & STATUS_OWNERSHIP_MASK) == ownership)
				{
					(*mask) &= ~STATUS_OWNERSHIP_MASK;
					(*mask) |= resolve_domains;
					self->listener->resolve(domain, (BLOCK_WIDTH * x) & (FB_WIDTH - 1), (BLOCK_HEIGHT * y) & (FB_HEIGHT - 1));
				}
			}
		}
	}

	static Domain fbatlas_find_suitable_domain(FBAtlas *self, const Rect &rect)
	{
		if (fbatlas_inside_render_pass(self, rect))
			return Domain_Scaled;

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;

		for (unsigned y = ybegin; y <= yend; y++)
		{
			for (unsigned x = xbegin; x <= xend; x++)
			{
				unsigned i = (*fbatlas_info(self, x, y)) & STATUS_OWNERSHIP_MASK;
				if (i == STATUS_FB_ONLY || i == STATUS_FB_PREFER)
					return Domain_Unscaled;
			}
		}
		return Domain_Scaled;
	}

	static bool fbatlas_inside_render_pass(FBAtlas *self, const Rect &rect)
	{
		if (!self->renderpass.inside)
			return false;

		unsigned xbegin = rect.x & ~(BLOCK_WIDTH - 1);
		unsigned ybegin = rect.y & ~(BLOCK_HEIGHT - 1);
		unsigned xend = ((rect.x + rect.width - 1) | (BLOCK_WIDTH - 1)) + 1;
		unsigned yend = ((rect.y + rect.height - 1) | (BLOCK_HEIGHT - 1)) + 1;

		unsigned rpx2 = self->renderpass.rect.x + self->renderpass.rect.width;
		unsigned rpy2 = self->renderpass.rect.y + self->renderpass.rect.height;
		unsigned x0 = (self->renderpass.rect.x > xbegin) ? self->renderpass.rect.x : xbegin;
		unsigned x1 = (rpx2 < xend) ? rpx2 : xend;
		unsigned y0 = (self->renderpass.rect.y > ybegin) ? self->renderpass.rect.y : ybegin;
		unsigned y1 = (rpy2 < yend) ? rpy2 : yend;

		return x1 > x0 && y1 > y0;
	}

	static void fbatlas_flush_render_pass(FBAtlas *self)
	{
		if (!self->renderpass.inside)
			return;

		// Clear out the "shadow" stage.
		{ unsigned _i; for (_i = 0; _i < NUM_BLOCKS_X * NUM_BLOCKS_Y; _i++)
			self->fb_info[_i] &= ~STATUS_TEXTURE_READ; }

		self->renderpass.inside = false;
		const Rect &rect = self->renderpass.rect;
		if (rect.width == 0 || rect.height == 0)
			return;

		fbatlas_write_domain(self, Domain_Scaled, Stage_Fragment, rect);
		self->listener->flush_render_pass(rect);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				(*fbatlas_info(self, x, y)) |= STATUS_TEXTURE_RENDERED;
	}

	static void fbatlas_extend_render_pass(FBAtlas *self, const Rect &rect, bool scissor)
	{
		bool scissor_invariant = !scissor || self->renderpass.scissor.contains(rect);
		self->listener->set_scissored_invariant(scissor_invariant);
		Rect scissored_rect = !scissor_invariant ? rect.scissor(self->renderpass.scissor) : rect;

		if (!scissored_rect.width || !scissored_rect.height)
			return;

		if (!self->renderpass.inside)
		{
			self->renderpass.rect = scissored_rect;
			fbatlas_sync_domain(self, Domain_Scaled, self->renderpass.rect);
			fbatlas_write_domain(self, Domain_Scaled, Stage_Fragment, self->renderpass.rect);
			self->renderpass.inside = true;
		}
		else if (!self->renderpass.rect.contains(scissored_rect))
		{
			self->renderpass.rect.extend_bounding_box(scissored_rect);

			// Avoid sync/write domain flushing our own render pass.
			self->renderpass.inside = false;

			// If we cleared the screen and we created a clear candidate,
			// everything inside this render pass can be safely discarded.
			if (!scissor && scissored_rect == self->renderpass.rect)
				fbatlas_discard_render_pass(self);

			fbatlas_sync_domain(self, Domain_Scaled, self->renderpass.rect);
			if (fbatlas_write_domain(self, Domain_Scaled, Stage_Fragment, self->renderpass.rect))
			{
				// If render pass was flushed here due to write-after-read hazards, set rect to
				// our new scissored_rect instead.
				self->renderpass.inside = true;
				fbatlas_flush_render_pass(self);
				self->renderpass.rect = scissored_rect;
			}

			self->renderpass.inside = true;
		}
	}

	static void fbatlas_write_fragment(FBAtlas *self, Domain domain, const Rect &rect)
	{
		bool reads_window = self->renderpass.texture_mode != TextureMode_None;
		if (reads_window)
		{
			Rect shifted = self->renderpass.texture_window;
			bool reads_palette;
			switch (self->renderpass.texture_mode)
			{
				case TextureMode_Palette4bpp:
				case TextureMode_Palette8bpp:
					reads_palette = true;
					break;

				default:
					reads_palette = false;
					break;
			}
			shifted.x += self->renderpass.texture_offset_x;
			shifted.y += self->renderpass.texture_offset_y;

			const Rect palette_rect = { self->renderpass.palette_offset_x, self->renderpass.palette_offset_y,
				self->renderpass.texture_mode == TextureMode_Palette8bpp ? 256u : 16u, 1 };

			if (reads_palette)
			{
				if (fbatlas_inside_render_pass(self, shifted) || fbatlas_inside_render_pass(self, palette_rect))
					fbatlas_flush_render_pass(self);
			}
			else if (fbatlas_inside_render_pass(self, shifted))
				fbatlas_flush_render_pass(self);

			fbatlas_read_texture(self, domain);
		}

		fbatlas_extend_render_pass(self, rect, true);
	}

	static void fbatlas_clear_rect(FBAtlas *self, const Rect &rect, uint32_t fb_color)
	{
		if (rect.width == 0 || rect.height == 0)
			return;

		// If we're clearing completely outside the self->renderpass, we're probably doing another render pass
		// somewhere else, so end the current one and start a new one instead.
		if (self->renderpass.inside && !self->renderpass.rect.intersects(rect))
			fbatlas_flush_render_pass(self);

		fbatlas_extend_render_pass(self, rect, false);

		// If the render pass area doesn't increase later, we can use loadOp == CLEAR instead of LOAD,
		// which helps a lot on mobile GPUs.
		self->listener->clear_quad(rect, fb_color, self->renderpass.rect == rect);

		unsigned xbegin = rect.x / BLOCK_WIDTH;
		unsigned xend = (rect.x + rect.width - 1) / BLOCK_WIDTH;
		unsigned ybegin = rect.y / BLOCK_HEIGHT;
		unsigned yend = (rect.y + rect.height - 1) / BLOCK_HEIGHT;
		for (unsigned y = ybegin; y <= yend; y++)
			for (unsigned x = xbegin; x <= xend; x++)
				(*fbatlas_info(self, x, y)) &= ~STATUS_TEXTURE_RENDERED;
	}

	static void fbatlas_discard_render_pass(FBAtlas *self)
	{
		self->renderpass.inside = false;
		self->listener->discard_render_pass();
	}

	static void fbatlas_notify_external_barrier(FBAtlas *self, StatusFlags domains)
	{
		static const StatusFlags compute_read_stages = STATUS_COMPUTE_FB_READ | STATUS_COMPUTE_SFB_READ;
		static const StatusFlags compute_write_stages = STATUS_COMPUTE_FB_WRITE | STATUS_COMPUTE_SFB_WRITE;
		static const StatusFlags transfer_read_stages = STATUS_TRANSFER_FB_READ | STATUS_TRANSFER_SFB_READ;
		static const StatusFlags transfer_write_stages = STATUS_TRANSFER_FB_WRITE | STATUS_TRANSFER_SFB_WRITE;
		static const StatusFlags fragment_write_stages = STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_FB_WRITE;
		static const StatusFlags fragment_read_stages = STATUS_FRAGMENT_SFB_READ | STATUS_FRAGMENT_FB_READ;

		if (domains & compute_write_stages)
			domains |= compute_write_stages | compute_read_stages;
		if (domains & compute_read_stages)
			domains |= compute_read_stages;
		if (domains & transfer_write_stages)
			domains |= transfer_write_stages | transfer_read_stages;
		if (domains & transfer_read_stages)
			domains |= transfer_read_stages;
		if (domains & fragment_write_stages)
			domains |= fragment_write_stages | fragment_read_stages;
		if (domains & fragment_read_stages)
			domains |= fragment_read_stages;

		{ unsigned _i; for (_i = 0; _i < NUM_BLOCKS_X * NUM_BLOCKS_Y; _i++)
			self->fb_info[_i] &= ~domains; }
	}

	static void fbatlas_pipeline_barrier(FBAtlas *self, StatusFlags domains)
	{
		if (domains & (STATUS_FRAGMENT_SFB_WRITE | STATUS_FRAGMENT_SFB_READ))
			fbatlas_flush_render_pass(self);
		self->listener->hazard(domains);
		fbatlas_notify_external_barrier(self, domains);
	}

/* ============================================================
 *
 * Folded content from the parallel-psx/custom-textures/
 * source files and the previously-private image_io.hpp +
 * dbg_input_callback.h headers.
 *
 * ============================================================ */

/* === config_parser.cpp === */

/* Whitespace per std::regex ECMAScript \s: space, tab, newline, CR, FF, VT. */
static inline int cfg_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static inline const char *cfg_skip_ws(const char *p)
{
    while (*p && cfg_is_space((unsigned char)*p)) p++;
    return p;
}

/* Parse one "(\d+|\*)" field with surrounding optional whitespace. On success
 * advances *pp past the field and returns 0, writing the value (-1 for '*');
 * returns -1 if the field doesn't match. */
static int cfg_parse_field(const char **pp, int *out)
{
    const char *p = cfg_skip_ws(*pp);
    if (*p == '*') {
        *out = -1;
        p++;
    } else if (*p >= '0' && *p <= '9') {
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        *out = v;
    } else {
        return -1;
    }
    *pp = cfg_skip_ws(p);
    return 0;
}

/* Match one config line against:
 *   ^\s*ignore\s+(\d+|\*)\s*,\s*(\d+|\*)\s*,\s*(\d+|\*)\s*,\s*(\d+|\*)\s*(?:#.*)?$
 * On match, fills *m and returns true. Hand-rolled replacement for std::regex. */
static bool cfg_match_ignore(const char *line, RectMatch *m)
{
    const char *p = cfg_skip_ws(line);
    int i;
    int *fields[4];
    if (strncmp(p, "ignore", 6) != 0)
        return false;
    p += 6;
    /* \s+ : at least one whitespace after the keyword */
    if (!cfg_is_space((unsigned char)*p))
        return false;
    p = cfg_skip_ws(p);

    fields[0] = &m->x; fields[1] = &m->y; fields[2] = &m->w; fields[3] = &m->h;
    for (i = 0; i < 4; i++) {
        if (cfg_parse_field(&p, fields[i]) != 0)
            return false;
        if (i < 3) {
            if (*p != ',')
                return false;
            p++;
        }
    }
    /* optional trailing comment, then end of line */
    if (*p == '#')
        return true;
    return *p == '\0';
}

static int parse_config_file(const char *path, RectMatch *out, int max) {
    char line[1024];
    int count = 0;
    RFILE *in = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
                                RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!in)
        return 0;
    while (count < max && filestream_gets(in, line, sizeof(line))) {
        RectMatch m;
        if (cfg_match_ignore(line, &m))
            out[count++] = m;
    }
    filestream_close(in);
    return count;
}

/* === image_io.hpp (private; only used within this TU) === */


struct RGBAImage {
    uint8_t* data;
    int width;
    int height;
};

// Decode an image from disk into *img (RGBA, 4 channels). On failure img->data
// is NULL. The caller owns img->data and must release it with rgba_image_free.
void load_image(const char *path, RGBAImage *img);
void rgba_image_free(RGBAImage *img);
bool write_image(const char *path, int width, int height, const void *data);

/* === image_io.cpp === */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb/stb_image.h"

void rgba_image_free(RGBAImage *img) {
    if (img->data != NULL) {
        stbi_image_free(img->data);
        img->data = NULL;
    }
}

void load_image(const char *path, RGBAImage *img) {
    int channels;
    int64_t size;
    void *buf      = NULL;
    img->data      = NULL;
    if (filestream_read_file(path, &buf, &size) && buf) {
        img->data = stbi_load_from_memory((const stbi_uc *) buf, (int) size,
                                          &img->width, &img->height, &channels, 4);
        free(buf);
    }
}

static void rhi_stbi_vfs_write(void *context, void *data, int size) {
    filestream_write((RFILE *) context, data, (int64_t) size);
}

bool write_image(const char *path, int width, int height, const void *data) {
    RFILE *f = filestream_open(path,
            RETRO_VFS_FILE_ACCESS_WRITE,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!f)
        return false;
    int ok = stbi_write_png_to_func(rhi_stbi_vfs_write, f,
                                    width, height, 4, data, 4 * width);
    filestream_close(f);
    return ok != 0;
}

/* === dbg_input_callback.h (extern, defined in libretro.c) === */

extern retro_input_state_t dbg_input_state_cb;


/* === texture_tracker.cpp === */
#include "libretro-common/include/retro_dirent.h"

/* Actually using the implementation in deps/zlib/crc32.c I think */
#include "scrc32.h"

extern char retro_cd_base_name[4096];
extern char retro_cd_base_directory[4096];
#ifdef _WIN32
static char retro_slash = '\\';
#else
static char retro_slash = '/';
#endif

	// Path helpers write into a caller-provided buffer (PATH_MAX_TT bytes) and
	// return it, C-style, instead of allocating a std::string.

	char *dump_path(char *out, size_t cap) {
		snprintf(out, cap, "%s%c%s-texture-dump%c",
				retro_cd_base_directory, retro_slash, retro_cd_base_name, retro_slash);
		return out;
	}

	char *replacements_path(char *out, size_t cap) {
		snprintf(out, cap, "%s%c%s-texture-replacements%c",
				retro_cd_base_directory, retro_slash, retro_cd_base_name, retro_slash);
		return out;
	}

	char *replacement_filename_from_hash(char *out, size_t cap, uint32_t hash, uint32_t palette_hash) {
		char base[PATH_MAX_TT];
		replacements_path(base, sizeof(base));
		snprintf(out, cap, "%s%x-%x.png", base, (unsigned)hash, (unsigned)palette_hash);
		return out;
	}

	static inline uint8_t *loaded_pixel(LoadedImage &image, int x, int y) {
		return &image.owned_data[(y * image.width + x) * 4];
	}

	LoadedImage generate_mip(LoadedImage &higher) {
		// Generate custom mipmaps in order to avoid transparent (0, 0, 0, 0) and semi-transparent (r, g, b, a>=128)
		// mixing to create some dark opaque value (r, g, b, a<128).

		LoadedImage result;
		int x, y;
		// Assumes higher.width and higher.height are both divisible by 2 (and also therefore > 1)
		loaded_image_init(&result);
		loaded_image_alloc(&result, higher.width / 2, higher.height / 2);
		for (y = 0; y < result.height; y++) {
			for (x = 0; x < result.width; x++) {
				uint8_t *src00 = loaded_pixel(higher, x * 2 + 0, y * 2 + 0);
				uint8_t *src10 = loaded_pixel(higher, x * 2 + 1, y * 2 + 0);
				uint8_t *src01 = loaded_pixel(higher, x * 2 + 0, y * 2 + 1);
				uint8_t *src11 = loaded_pixel(higher, x * 2 + 1, y * 2 + 1);

				int numTransparent = 0;
				if (src00[0] == 0 && src00[1] == 0 && src00[2] == 0 && src00[3] == 0) numTransparent += 1;
				if (src10[0] == 0 && src10[1] == 0 && src10[2] == 0 && src10[3] == 0) numTransparent += 1;
				if (src01[0] == 0 && src01[1] == 0 && src01[2] == 0 && src01[3] == 0) numTransparent += 1;
				if (src11[0] == 0 && src11[1] == 0 && src11[2] == 0 && src11[3] == 0) numTransparent += 1;

				uint8_t *dst = loaded_pixel(result, x, y);
				if (numTransparent > 2) {
					dst[0] = 0;
					dst[1] = 0;
					dst[2] = 0;
					dst[3] = 0;
				} else {
					int r = src00[0] + src10[0] + src01[0] + src11[0];
					int g = src00[1] + src10[1] + src01[1] + src11[1];
					int b = src00[2] + src10[2] + src01[2] + src11[2];
					int a = src00[3] + src10[3] + src01[3] + src11[3];

					int numNotTransparent = 4 - numTransparent;
					dst[0] = r / numNotTransparent;
					dst[1] = g / numNotTransparent;
					dst[2] = b / numNotTransparent;
					dst[3] = a / numNotTransparent;
				}
			}
		}
		return result;
	}

	LoadedImage convert_tri_to_psx(uint8_t *image, int width, int height, int& alpha_flags) {
		LoadedImage result;
		size_t i;
		loaded_image_init(&result);
		loaded_image_alloc(&result, width, height);
		alpha_flags = 0;
		for (i = 0; i < result.owned_size; i += 4) {
			uint8_t *src = &image[i];
			uint8_t *dst = &result.owned_data[i];
			if (src[3] == 0) {
				// Transparent
				alpha_flags |= ALPHA_FLAG_TRANSPARENT;
				dst[0] = 0;
				dst[1] = 0;
				dst[2] = 0;
				dst[3] = 0;
			} else if (src[3] == 255) {
				alpha_flags |= ALPHA_FLAG_OPAQUE;
				if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
					// Opaque black
					dst[0] = 1;
					dst[1] = 1;
					dst[2] = 1;
					dst[3] = 0;
				} else {
					// Opaque
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 0;
				}
			} else {
				alpha_flags |= ALPHA_FLAG_SEMI_TRANSPARENT;
				if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
					// (0, 0, 0, 255) is a special reserved value
					dst[0] = 1;
					dst[1] = 1;
					dst[2] = 1;
					dst[3] = 255;
				} else {
					// Semi-transparent
					dst[0] = src[0];
					dst[1] = src[1];
					dst[2] = src[2];
					dst[3] = 255;
				}
			}
		}
		return result;
	}

	LoadedLevels prepare_texture(RGBAImage &image, int& alpha_flags) {
		LoadedLevels levels;
		int width = image.width;
		int height = image.height;
		LoadedImage base;
		loaded_levels_init(&levels);
		base = convert_tri_to_psx(image.data, width, height, alpha_flags);
		loaded_levels_push_move(&levels, &base);
		while (width % 2 == 0 && height % 2 == 0) {
			LoadedImage mip = generate_mip(levels.levels[levels.count - 1]);
			loaded_levels_push_move(&levels, &mip);

			width /= 2;
			height /= 2;
		}
		return levels;
	}

	/*
	   void convert_psx_to_tri(uint8_t *image, int width, int height) {
	   for (int i = 0; i < width * height * 4; i += 4) {
	   uint8_t *pixel = &image[i];
	   if (pixel[3] == 0) {
	   if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
	// Transparent
	// do nothing, pixel is already in the correct format
	} else {
	// Opaque
	pixel[3] = 255;
	}
	} else {
	// Semi-transparent
	pixel[3] = 127;
	}
	}
	}
	 */

	void io_thread(void *user_data) {
		// Pool worker. Each worker is handed the channel pointer with a reference
		// already taken on its behalf (at spawn); it releases that reference on the
		// way out. Whichever holder releases last frees the channel.
		IOChannel *channel = (IOChannel *)user_data;
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread starting\n");

		while (true) {
			IORequest *request = NULL;
			{
				slock_lock(channel->lock);
				while (channel->req_head == NULL && !channel->done) {
					scond_wait(channel->cond, channel->lock);
				}
				if (channel->done) {
					// Prompt shutdown; drop any unprocessed requests (matches the
					// previous single-thread behaviour). The channel destructor
					// frees whatever is still queued.
					slock_unlock(channel->lock);
					break;
				}
				// Take ONE request from the front so work spreads across the pool
				// and the producer's priority order (visible palette first) is
				// preserved. Wake another worker if more remain.
				request = io_channel_pop_request(channel);
				if (channel->req_head != NULL) {
					scond_signal(channel->cond);
				}
				slock_unlock(channel->lock);
			}

			// The expensive part (PNG decode + mipmaps, or PNG write) runs WITHOUT
			// the lock so workers process in parallel; only the queue access and
			// the response push are serialised.
			if (request->kind == IORequestKind_Load) {
				uint32_t hash = request->hash;
				uint32_t palette_hash = request->palette_hash;

				char path[PATH_MAX_TT];
				RGBAImage image;
				replacement_filename_from_hash(path, sizeof(path), hash, palette_hash);
				image.data = NULL;
				load_image(path, &image);
				if (image.data != NULL) {
					int alpha_flags_out = 0;
					LoadedLevels levels = prepare_texture(image, alpha_flags_out);
					IOResponse *response = (IOResponse *)malloc(sizeof(IOResponse));
					response->next         = NULL;
					response->hash         = hash;
					response->palette_hash = palette_hash;
					response->alpha_flags  = alpha_flags_out;
					loaded_levels_init(&response->levels);
					loaded_levels_move(&response->levels, &levels);

					slock_lock(channel->lock);
					io_channel_push_response(channel, response);
					slock_unlock(channel->lock);

					rgba_image_free(&image);
				} else {
					TT_LOG(RETRO_LOG_ERROR, "failed to load: %s\n", path);
				}
			} else if (request->kind == IORequestKind_Dump) {
				int success = write_image(request->path, request->width, request->height, request->bytes);
				if (success == 0) {
					TT_LOG(RETRO_LOG_ERROR, "failed to write to: %s\n", request->path);
				}
			}
			io_request_free(request);
		}
		io_channel_release(channel); /* drop this worker's reference */
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread ending\n");
	}

	static void io_channel_destroy(IOChannel *c) {
		/* Free any nodes still queued at shutdown. */
		IORequest *r = c->req_head;
		IOResponse *p = c->resp_head;
		while (r) { IORequest *n = r->next; io_request_free(r); r = n; }
		while (p) { IOResponse *n = p->next; io_response_free(p); p = n; }
		slock_free(c->lock);
		scond_free(c->cond);
		free(c);
	}

	// Number of parallel PNG-decode workers. Keeps first-appearance prefetch
	// bursts short without starving the emulation/render threads.
	static const int NUM_IO_THREADS = 4;

	void io_thread_init(IOThread *t) {
		io_channel_rc_lock_init();
		t->channel = io_channel_new(); /* this IOThread holds one reference */
		for (int i = 0; i < NUM_IO_THREADS; i++) {
			// Take a reference on the worker's behalf BEFORE it starts, so the
			// channel can't be freed out from under it; the worker releases on exit.
			io_channel_acquire(t->channel);
			sthread_t *thread = sthread_create(io_thread, t->channel);
			if (thread) {
				sthread_detach(thread);
			} else {
				io_channel_release(t->channel); // thread failed to start; undo its ref
			}
		}
	}
	void io_thread_deinit(IOThread *t) {
		slock_lock(t->channel->lock);
		t->channel->done = true;
		slock_unlock(t->channel->lock);
		scond_broadcast(t->channel->cond); // wake ALL workers so they can exit
		io_channel_release(t->channel);    // drop this IOThread's reference; the last
						// worker to exit frees the channel
		t->channel = NULL;
	}

	void texture_tracker_dump_image(struct TextureTracker *self, TextureUpload &upload, UsedMode &mode) {
		uint32_t hash = upload.hash;

		uint8_t *bytes;
		size_t   bytes_len;
		size_t   bi;
		size_t   img_count;

		// from glsl/vram.h
		int shift;
		switch (mode.mode) {
			case TextureMode_ABGR1555:
				shift = 0;
				break;
			case TextureMode_Palette8bpp:
				shift = 1;
				break;
			case TextureMode_Palette4bpp:
				shift = 2;
				break;
			case TextureMode_None:
			default:
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Tried to dump unused texture %x.\n", hash);
				return; // Early out
		}

		char path[PATH_MAX_TT];
		size_t plen;
		dump_path(path, sizeof(path));
		plen = strlen(path);
		snprintf(path + plen, sizeof(path) - plen, "%x", (unsigned)hash);

		uint16_t *palette = NULL;
		if (mode.mode == TextureMode_Palette4bpp || mode.mode == TextureMode_Palette8bpp) {
			Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode_Palette8bpp ? 256 : 16, 1);
			Palette p = texture_tracker_get_palette(self, palette_rect);
			if (p.data != NULL) {
				palette = p.data;
				plen = strlen(path);
				snprintf(path + plen, sizeof(path) - plen, "-%x", (unsigned)p.hash);
			}
		}

		if (palette != NULL) {
			TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
		} else if (mode.mode != TextureMode_ABGR1555) {
			plen = strlen(path);
			snprintf(path + plen, sizeof(path) - plen, "-missing");
			TT_LOG_VERBOSE(RETRO_LOG_INFO, "MISSING palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
		}

		int ppp = 1 << shift;
		int bpp = 16 >> shift;
		int mask = (1 << bpp) - 1;
		/* Output is exactly 4 bytes per (subpixel) = image.size() * ppp * 4. */
		img_count = (size_t)upload.image_count;
		bytes_len = img_count * (size_t)ppp * 4u;
		bytes = (uint8_t *)malloc(bytes_len ? bytes_len : 1);
		bi = 0;
		{
			size_t ii;
			for (ii = 0; ii < img_count; ii++) {
				uint16_t pixel = upload.image[ii];
				int p;
				for (p = 0; p < ppp; p++) {
					uint16_t subpixel = (pixel >> (p * bpp)) & mask;
					if (mode.mode != TextureMode_ABGR1555 && palette == NULL) {
						// Missing palette, dump a grayscale version of the image data
						bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
						bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
						bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
						bytes[bi++] = (uint8_t)255;
					} else {
						uint16_t abgr1555;
						if (mode.mode == TextureMode_ABGR1555) {
							abgr1555 = subpixel;
						} else {
							abgr1555 = palette[subpixel];
						}
						int r = ((abgr1555 >> 0) & 0x1f) * 255.0 / 31.0;
						int g = ((abgr1555 >> 5) & 0x1f) * 255.0 / 31.0;
						int b = ((abgr1555 >> 10) & 0x1f) * 255.0 / 31.0;
						int a = (abgr1555 >> 15) * 255.0;
						// Convert psx to tri
						if (a == 0) {
							if (r == 0 && g == 0 && b == 0) {
								// Transparent
								// do nothing, pixel is already in the correct format
							} else {
								// Opaque
								a = 255;
							}
						} else {
							// Semi-transparent
							a = 127;
						}
						bytes[bi++] = (uint8_t)r;
						bytes[bi++] = (uint8_t)g;
						bytes[bi++] = (uint8_t)b;
						bytes[bi++] = (uint8_t)a;
					}
				}
			}
		}

		plen = strlen(path);
		snprintf(path + plen, sizeof(path) - plen, ".png");

		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dump info: mode=%i, w=%i, h=%i, len=%i, bytesLen=%i\n", mode.mode, upload.width, upload.height, upload.image_count, (int)bytes_len);
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping to %s.\n", path);

		//stbi_write_png(path, upload.width * ppp, upload.height, 4, bytes, 4 * upload.width * ppp);
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting dump: %s\n", path);
		{
			IORequest *dump = (IORequest *)malloc(sizeof(IORequest));
			dump->next = NULL;
			dump->kind = IORequestKind_Dump;
			snprintf(dump->path, sizeof(dump->path), "%s", path);
			dump->width = upload.width * ppp;
			dump->height = upload.height;
			dump->bytes = bytes;          /* transfer ownership to the request */
			dump->bytes_len = bytes_len;

			slock_lock(self->iothread.channel->lock);
			io_channel_push_request(self->iothread.channel, dump);
			slock_unlock(self->iothread.channel->lock);
			scond_signal(self->iothread.channel->cond);
		}
	}

	void read_texture_directory(HdKeySet *out, const char *path) {
		RDIR *dir;
		hd_key_set_clear(out);
		dir = retro_opendir(path);
		if (dir != NULL) {
			while (retro_readdir(dir)) {
				// https://stackoverflow.com/questions/13701657/control-whole-string-with-sscanf
				uint32_t hash;
				uint32_t palette_hash;
				int chars_read;
				const char *name = retro_dirent_get_name(dir);
				if (sscanf(name, "%x-%x.png%n", &hash, &palette_hash, &chars_read) != 2 ||
						chars_read != strlen(name)
				   ) {
					continue;
				}

				hd_key_set_insert(out, ((uint64_t)hash << 32) | (uint64_t)palette_hash);
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "file found: %s\n", name);
			}
			retro_closedir(dir);
		}
	}

	void texture_tracker_init(struct TextureTracker *self)
	{
		char rpath[PATH_MAX_TT];
		char cfg[PATH_MAX_TT];
		int mi;
		/* former default member initializers */
		self->dump_enabled = false;
		self->hd_textures_enabled = false;
		self->eager_textures = true;
		self->uploader = NULL;
		self->frame = 0;
		self->dbg_responses_received = 0;
		self->dbg_responses_received_last = 0;
		self->dbg_gpu_uploads = 0;
		self->dbg_gpu_uploads_last = 0;
		self->dbg_attaches = 0;
		self->dbg_attaches_last = 0;
		self->frame_dump = NULL;
		self->frame_dump_need_comma = false;
		self->fastpath_enabled = true;
		self->default_hd_texture.data = NULL; /* plain-struct handle: explicit null */
		rect_tracker_init(&self->tracker); /* embedded RectTracker: explicit init (was default ctor) */
		fused_pages_init(&self->fused_pages); /* embedded FusedPages: explicit init (was default ctor) */
		HdImageCache_init(&self->hd_cache, HD_CACHE_RAM_BUDGET);
		HdGpuCache_init(&self->hd_gpu_cache, HD_CACHE_VRAM_BUDGET);
		handle_lru_cache_init(&self->handle_cache, 4);
		dbg_hotkey_init(&self->frame_dump_key, RETROK_LEFTBRACKET);
		dbg_hotkey_init(&self->hd_toggle_key, RETROK_RIGHTBRACKET);
		dbg_hotkey_init(&self->reload_key, RETROK_QUOTE);
		dbg_hotkey_init(&self->fastpath_key, RETROK_SEMICOLON);
		hd_key_set_init(&self->known_files);
		hd_key_set_init(&self->requested);
		hd_key_set_init(&self->pending_attach);
		self->cached_palette_hashes = NULL;
		self->cached_palette_hashes_count = 0;
		self->cached_palette_hashes_cap = 0;
		read_texture_directory(&self->known_files, replacements_path(rpath, sizeof(rpath)));
		TT_LOG(RETRO_LOG_INFO, "num hd textures: %d\n", (int)self->known_files.count);

		// Read in the dump config file
		dump_path(cfg, sizeof(cfg));
		snprintf(cfg + strlen(cfg), sizeof(cfg) - strlen(cfg), "/dump.cfg");
		self->dump_ignore_count = parse_config_file(cfg, self->dump_ignore, DUMP_IGNORE_MAX);
		for (mi = 0; mi < self->dump_ignore_count; mi++) {
			RectMatch m = self->dump_ignore[mi];
			TT_LOG_VERBOSE(RETRO_LOG_INFO, "Ignoring %d,%d,%d,%d\n", m.x, m.y, m.w, m.h);
		}
		/* Spin up the IO worker pool last, once the self->tracker is fully built. */
		io_thread_init(&self->iothread);
	}

	void texture_tracker_fini(struct TextureTracker *self)
	{
		ih_reset(&self->default_hd_texture); /* drop the default HD texture reference */
		HdImageCache_clear(&self->hd_cache);   /* frees decoded levels + arena */
		HdGpuCache_clear(&self->hd_gpu_cache); /* releases cached image refs + arena */
		hd_key_set_free(&self->known_files);
		hd_key_set_free(&self->requested);
		hd_key_set_free(&self->pending_attach);
		free(self->cached_palette_hashes);
		fused_pages_deinit(&self->fused_pages); /* embedded FusedPages: explicit deinit (was default dtor) */
		rect_tracker_deinit(&self->tracker); /* embedded RectTracker: explicit deinit (was default dtor) */
		/* Signal and drop the IO worker pool last. Previously self->iothread was a
		 * by-value member, so its destructor ran after this body; preserve that
		 * ordering by deinitialising it here at the end. */
		io_thread_deinit(&self->iothread);
	}

	static inline SRect toSRect(Rect rect) {
		return SRect(rect.x, rect.y, rect.width, rect.height);
	}
	static inline Rect fromSRect(SRect rect) {
		return Rect(rect.x, rect.y, rect.width, rect.height);
	}

	Palette texture_tracker_get_palette(struct TextureTracker *self, Rect palette_rect) {
		assert(palette_rect.height == 1);

		static RectIndexSet overlap = { NULL, 0, 0 };
		rect_tracker_overlapping(&self->tracker, palette_rect, &overlap);
		for (int oi = 0; oi < overlap.count; oi++) {
			RectIndex index = overlap.items[oi];
			EnduringTextureRect &other = self->tracker.textures.a[index]; // TODO: The `other.alive` check is unnecessary because self->tracker.overlapping never returns dead indices
			if (fromSRect(other.texture_rect.vram_rect).contains(palette_rect) && other.alive) {
				if (other.texture_rect.offset_x != 0 || other.texture_rect.offset_y != 0) {
					continue; // TODO: handle offset subrects
				}
				int x = palette_rect.x - other.texture_rect.vram_rect.x;
				int y = palette_rect.y - other.texture_rect.vram_rect.y;
				int offset = y * other.texture_rect.vram_rect.width + x;
				uint16_t *data = other.texture_rect.upload->image + offset;
				uint32_t hash = crc32(0, (unsigned char*)data, palette_rect.width * sizeof(uint16_t));
				return { data, hash };
			}
		}
		return { NULL, 0 };
	}

	uint32_t texture_tracker_get_palette_hash(struct TextureTracker *self, Rect palette_rect) {
		int i;
		for (i = 0; i < self->cached_palette_hashes_count; i++) {
			if (self->cached_palette_hashes[i].rect == palette_rect) {
				return self->cached_palette_hashes[i].hash;
			}
		}
		Palette palette = texture_tracker_get_palette(self, palette_rect);
		if (palette.data != NULL) {
			if (self->cached_palette_hashes_count == self->cached_palette_hashes_cap) {
				int ncap = self->cached_palette_hashes_cap ? self->cached_palette_hashes_cap * 2 : 16;
				CachedPaletteHash *nh = (CachedPaletteHash *)realloc(self->cached_palette_hashes,
						(size_t)ncap * sizeof(CachedPaletteHash));
				if (!nh)
					return palette.hash;
				self->cached_palette_hashes = nh;
				self->cached_palette_hashes_cap = ncap;
			}
			self->cached_palette_hashes[self->cached_palette_hashes_count].rect = palette_rect;
			self->cached_palette_hashes[self->cached_palette_hashes_count].hash = palette.hash;
			self->cached_palette_hashes_count++;
			return palette.hash;
		}
		return 0; // TODO: better way to indicate no palette found?
	}

	void texture_tracker_clearRegion(struct TextureTracker *self, Rect rect) {
		if (rect.width == 0 || rect.height == 0) {
			// Some games do this, apparently.
			return;
		}
		rect_tracker_clear(&self->tracker, SRect(rect.x, rect.y, rect.width, rect.height));
		fused_pages_mark_dead(&self->fused_pages, rect);

		texture_tracker_clear_palette_cache(self, rect);
	}

	bool imageMatches(TextureUpload &upload, Rect rect, uint16_t *vram) {
		unsigned x = rect.x,
			 y = rect.y,
			 w = rect.width,
			 h = rect.height;
		int index = 0;
		unsigned i, j;
		for (j = y; j < y + h; j++) {
			for (i = x; i < x + w; i++) {
				if (upload.image[index] != vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]) {
					return false;
				}
				index += 1;
			}
		}
		return true;
	}

	void texture_tracker_blit(struct TextureTracker *self, Rect dst, Rect src) {
		rect_tracker_blit(&self->tracker, SRect(dst.x, dst.y, dst.width, dst.height), SRect(src.x, src.y, src.width, src.height));
		fused_pages_mark_dirty(&self->fused_pages, dst);
		fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker, self->uploader);
		texture_tracker_clear_palette_cache(self, dst);
	}

	uint32_t texture_tracker_dbgHashVram(struct TextureTracker *self, Rect rect, uint16_t *vram) {
		unsigned x = rect.x,
			 y = rect.y,
			 w = rect.width,
			 h = rect.height;
		size_t n = (size_t)w * (size_t)h;
		uint16_t *buf = (uint16_t *)malloc(n * sizeof(uint16_t));
		size_t bi = 0;
		uint32_t hash;
		for (int j = y; j < (int)(y + h); j++) {
			for (int i = x; i < (int)(x + w); i++) {
				buf[bi++] = vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))];
			}
		}
		hash = crc32(0, (unsigned char*)buf, rect.width * rect.height * sizeof(uint16_t));
		free(buf);
		return hash;
	}

	/* Geometry helpers used to return std::pair<...,bool> (result + validity).
	 * Named structs make the boolean's meaning explicit and drop std::pair. */
	struct SRectResult { SRect rect; bool valid; };
	struct TextureRectResult { TextureRect rect; bool valid; };

	SRectResult intersect(SRect a, SRect b) {
		int al     = a.left(),   ar = a.right(),  at = a.top(), ab = a.bottom();
		int bl     = b.left(),   br = b.right(),  bt = b.top(), bb = b.bottom();
		int left   = (al > bl) ? al : bl;
		int right  = (ar < br) ? ar : br;
		int top    = (at > bt) ? at : bt;
		int bottom = (ab < bb) ? ab : bb;
		int width  = right - left;
		int height = bottom - top;
		if (width <= 0 || height <= 0)
		{ SRectResult r = { SRect(0, 0, 1, 1), false }; return r; }
		{ SRectResult r = { SRect(left, top, width, height), true }; return r; }
	}

	TextureRect subTexture(TextureRect original, SRect sub_vram_rect) {
		return make_texture_rect(
				original.upload,
				original.offset_x + sub_vram_rect.left() - original.vram_rect.left(),
				original.offset_y + sub_vram_rect.top() - original.vram_rect.top(),
				sub_vram_rect
				);
	}

	TextureRectResult clip_texture_rect_to_vram(TextureRect &t, Rect vram_rect) {
		SRectResult intersection = intersect(t.vram_rect, toSRect(vram_rect));
		if (intersection.valid) {
			TextureRectResult r = { subTexture(t, intersection.rect), true };
			return r;
		}
		TextureRectResult r = { make_texture_rect(NULL, 0, 0, SRect(0, 0, 1, 1)), false };
		return r;
	}

	void texture_tracker_notifyReadback(struct TextureTracker *self, Rect rect, uint16_t *vram) {
		// These hacks are my workaround for the dialog self->frame texture restorable getting evicted by FMVs
		if (rect.width == 96 && rect.height == 224 && rect.y == 0 && (rect.x % 96) == 0) {
			// HACK: Looks like final fmv self->frame readback for cross fade, ignore
			return;
		}
		if (rect.width == 64 && rect.height == 224 && rect.y == 0 && (rect.x % 64) == 0) {
			// HACK: Looks like final fmv self->frame readback for cross fade, ignore
			return;
		}

		uint32_t hash = texture_tracker_dbgHashVram(self, rect, vram);

		for (int i = 0; i < self->restorable_rects.size(); )
		{
			if (self->restorable_rects[i].rect.intersects(rect))
				self->restorable_rects.erase_at(i);
			else
				i++;
		}

		static RectIndexSet overlap = { NULL, 0, 0 };

		OwnedRectVec to_restore;
		rect_tracker_overlapping(&self->tracker, rect, &overlap);
		for (int oi = 0; oi < overlap.count; oi++) {
			RectIndex index = overlap.items[oi];
			EnduringTextureRect &e = self->tracker.textures.a[index];
			if (e.alive) { // TODO: This check is unnecessary because self->tracker.overlapping never returns dead indices
				       // Clip to the self->requested rect
				TextureRectResult result = clip_texture_rect_to_vram(e.texture_rect, rect);
				if (result.valid) {
					// assert(rect.contains(fromSRect(result.rect.vram_rect)));
					to_restore.push(result.rect);
				}
			}
		}

		RestorableRect rr;
		rr.rect = rect;
		rr.hash = hash;
		rr.to_restore = static_cast<OwnedRectVec &&>(to_restore);
		self->restorable_rects.push(rr);
	}

	void texture_tracker_upload(struct TextureTracker *self, Rect rect, uint16_t *vram) {
		texture_tracker_clear_palette_cache(self, rect);

		if (rect.width == FB_WIDTH && rect.height == FB_HEIGHT) {
			// probably loading a save state, this is the entirety of vram
			rect_tracker_clear(&self->tracker, toSRect(rect));
			fused_pages_mark_dead(&self->fused_pages, rect);
			return;
		}

		// Would this ever happen?
		if (rect.width == 0 || rect.height == 0) {
			return;
		}

		TextureUpload *upload = NULL;
		bool preexisting = false;
		{
			unsigned x = rect.x,
				 y = rect.y,
				 w = rect.width,
				 h = rect.height;
			size_t img_n = (size_t)w * (size_t)h;
			uint16_t *img = (uint16_t *)malloc(img_n * sizeof(uint16_t));
			size_t vi = 0;
			int j, i;
			for (j = y; j < (int)(y + h); j++) {
				for (i = x; i < (int)(x + w); i++) {
					img[vi++] = vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))];
				}
			}
			uint32_t hash = crc32(0, (unsigned char*)img, rect.width * rect.height * sizeof(uint16_t));
			// TODO: check for hash collision, by checking if existing upload has different dimensions. not sure how to recover if it does,
			//       but the odds of a collision are probably much higher than the odds that both textures would be in play simultaneously,
			//       so it'd probably be safe to simply ignore the newest upload and clear instead.
			upload = texture_tracker_find_upload(self, hash);    /* borrowed */
			if (upload == NULL) {
				upload = texture_upload_new();  /* owns +1 */
				upload->image = img;            /* transfer ownership */
				upload->image_count = (int)img_n;
				img = NULL;
				upload->width = rect.width;
				upload->height = rect.height;
				upload->hash = hash;
				upload->dumpable = true;
				// Don't dump uploads specified by dump.cfg
				for (int ri = 0; ri < self->dump_ignore_count; ri++) {
					if (rect_match_matches(&self->dump_ignore[ri], rect)) {
						upload->dumpable = false;
						break;
					}
				}
			} else {
				preexisting = true;
				texture_upload_acquire(upload); /* take our own ref on the borrowed result */
			}
			free(img); /* NULL if ownership was transferred */
		}

		RestorableRect *restore = NULL;
		for (RestorableRect &other : self->restorable_rects) {
			if (other.hash == upload->hash && other.rect == rect) {
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "RESTORATION: %x\n", other.hash);
				restore = &other;
				break;
			}
		}
		if (restore != NULL) {
			for (TextureRect &t : restore->to_restore) {
				rect_tracker_place(&self->tracker, t); // TODO: clip to other.rect
			}
		} else {
			rect_tracker_upload(&self->tracker, toSRect(rect), upload);
		}
		fused_pages_mark_dirty(&self->fused_pages, rect);
		fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker, self->uploader);

		// HD texture caching method:
		//  - Lazy (self->eager_textures=false): nothing is queued here; each (hash,palette)
		//    is loaded on demand when first drawn (request_hd_texture). Leanest.
		//  - Eager (self->eager_textures=true, the master-consistent default): on the first
		//    upload of a hash, prefetch ALL of its known palette variants into the
		//    cache. Routed through want_combo so it still respects the cache
		//    (decode-once / dedup) and the VRAM/RAM budgets, unlike stock Beetle's
		//    raw load_hd_texture.
		if (self->eager_textures && self->hd_textures_enabled && !preexisting) {
			int lo = hd_key_set_lower_bound(&self->known_files, (uint64_t)upload->hash << 32);
			int hi = hd_key_set_lower_bound(&self->known_files, ((uint64_t)upload->hash + 1) << 32);
			int ki;
			for (ki = lo; ki < hi; ki++) {
				HdTextureId combo;
				combo.hash = upload->hash;
				combo.palette_hash = (uint32_t)self->known_files.keys[ki];
				texture_tracker_want_combo(self, combo);
			}
		}
		texture_upload_release(upload); /* drop the local ref; rects hold their own */
	}

	void texture_tracker_load_hd_texture(struct TextureTracker *self, uint32_t hash) {
		int lo = hd_key_set_lower_bound(&self->known_files, (uint64_t)hash << 32);
		int hi = hd_key_set_lower_bound(&self->known_files, ((uint64_t)hash + 1) << 32);
		if (lo != hi) {
			int ki;
			slock_lock(self->iothread.channel->lock);
			for (ki = lo; ki < hi; ki++) {
				uint32_t palette_hash = (uint32_t)self->known_files.keys[ki];
				IORequest *load = (IORequest *)malloc(sizeof(IORequest));
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting texture: %x-%x\n", hash, palette_hash);
				load->next = NULL;
				load->kind = IORequestKind_Load;
				load->hash = hash;
				load->palette_hash = palette_hash;
				load->bytes = NULL;
				load->bytes_len = 0;
				io_channel_push_request(self->iothread.channel, load);
			}
			slock_unlock(self->iothread.channel->lock);
			scond_signal(self->iothread.channel->cond);
		}
	}

	// Queue a disk load for one (hash,palette) combo, unless it's already decoded
	// (in the cache), already in flight, or known to have no file. Combos with no
	// file are inserted into `requested` as a permanent negative cache. The IO
	// thread only pushes a response on success, so a failed/missing load stays in
	// `requested` and is never retried (until a reload clears it).
	void texture_tracker_want_combo(struct TextureTracker *self, HdTextureId id) {
		if (HdGpuCache_contains(&self->hd_gpu_cache, hd_pack_key(id)) || HdImageCache_contains(&self->hd_cache, hd_pack_key(id)))
			return; // already resident in VRAM, or already decoded in RAM
		if (!hd_key_set_insert(&self->requested, hd_pack_key(id)))
			return; // already in flight, or negatively cached
		if (!hd_key_set_contains(&self->known_files, hd_pack_key(id)))
			return; // no file on disk

		slock_lock(self->iothread.channel->lock);
		{
			IORequest *load = (IORequest *)malloc(sizeof(IORequest));
			load->next = NULL;
			load->kind = IORequestKind_Load;
			load->hash = id.hash;
			load->palette_hash = id.palette_hash;
			load->bytes = NULL;
			load->bytes_len = 0;
			io_channel_push_request(self->iothread.channel, load);
		}
		slock_unlock(self->iothread.channel->lock);
		scond_signal(self->iothread.channel->cond);
	}

	// Cache-backed HD texture binding for a drawn (hash,palette): pure lazy.
	//
	// If the combo is in the GPU cache, bind it immediately (handle copy, used this
	// frame). If it's decoded in the CPU cache, schedule a GPU upload for the next
	// safe point (on_queues_reset). Otherwise queue a single disk load for it. The
	// 3-tier cache makes every re-draw free, so each combo costs at most one decode
	// on its very first appearance.
	//
	// (Cross-hash prefetch was tried and removed: with the cache, re-draws are
	// already free, so warming the whole palette hash-set up front mostly decoded
	// combos that were never drawn - thrashing the RAM cache and clogging the IO
	// queue ahead of the combos actually on screen, which made pop-in worse.)
	void texture_tracker_request_hd_texture(struct TextureTracker *self, TextureUpload *upload, uint32_t palette_hash) {
		if (hd_tex_map_contains(&upload->textures, palette_hash))
			return; // already attached to this upload

		HdTextureId current = { upload->hash, palette_hash };

		// GPU-cache hit: the Vulkan image already exists, so binding it is just a
		// ref-counted handle copy (no Vulkan commands). Bind it IMMEDIATELY so the
		// CURRENT self->frame's draw uses the HD texture. Deferring to on_queues_reset
		// cost a 1-self->frame native flicker every time an animation self->frame's upload was
		// recreated (constant for sprites) - i.e. persistent pop-in even when the
		// image was fully cached.
		CachedGpuImage *gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(current));
		if (gpu != NULL) {
			hd_tex_map_set(&upload->textures, palette_hash, gpu->image, gpu->alpha_flags);
			self->dbg_attaches++;
			return;
		}

		// CPU-cache hit (decoded but not in VRAM): needs a GPU upload, which we keep
		// at the safe point - schedule it for on_queues_reset.
		if (HdImageCache_contains(&self->hd_cache, hd_pack_key(current)))
			hd_key_set_insert(&self->pending_attach, hd_pack_key(current));
		else
			texture_tracker_want_combo(self, current);            // queue a single disk load for the drawn combo
	}

	void output_rect_json(RFILE *stream, Rect &rect) {
		filestream_printf(stream,
				"{ \"x\": %u,\"y\": %u,\"width\": %u,\"height\": %u}\n",
				rect.x, rect.y, rect.width, rect.height);
	}

	void texture_tracker_dump_texture(struct TextureTracker *self, TextureUpload *upload, UsedMode &mode, DumpedMode dump_mode) {
		if (!upload->dumpable) {
			return;
		}

		bool already_dumped = false;
		int dmi;
		for (dmi = 0; dmi < upload->dumped_modes_count; dmi++) {
			if (upload->dumped_modes[dmi] == dump_mode) {
				already_dumped = true;
				break;
			}
		}
		if (!already_dumped) {
			if (upload->dumped_modes_count == upload->dumped_modes_cap) {
				int ncap = upload->dumped_modes_cap ? upload->dumped_modes_cap * 2 : 4;
				DumpedMode *nm = (DumpedMode *)realloc(upload->dumped_modes,
						(size_t)ncap * sizeof(DumpedMode));
				if (!nm)
					return;
				upload->dumped_modes = nm;
				upload->dumped_modes_cap = ncap;
			}
			upload->dumped_modes[upload->dumped_modes_count++] = dump_mode;
			if (self->dump_enabled) {
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping %x\n", upload->hash);
				texture_tracker_dump_image(self, *upload, mode);
			}
		}
	}

	HandleCacheResult HandleLRUCache::get(Rect rect, uint32_t palette_hash) {
		HandleCacheResult res;
		int i, j;
		for (i = 0; i < count; i++) {
			CacheEntry &entry = entries[i];
			if (entry.handle.palette_hash == palette_hash && entry.rect.contains(rect)) {
				CacheEntry hit = entry;
				for (j = i; j > 0; j--) {
					entries[j] = entries[j - 1];
				}
				entries[0] = hit;
				dbg_hits += 1;
				res.handle = hit.handle;
				res.found = true;
				return res;
			}
		}
		dbg_misses += 1;
		res.handle = HdTextureHandle::make_none();
		res.found = false;
		return res;
	}
	void HandleLRUCache::insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle) {
		int j;
		CacheEntry e;
		e.rect = rect;
		e.handle = handle;
		/* If full, the entry at index max_size-1 (the LRU) is dropped by the shift
		 * below not preserving it. Otherwise grow by one. */
		if (count < max_size)
			count++;
		for (j = count - 1; j > 0; j--)
			entries[j] = entries[j - 1];
		entries[0] = e;
	}

	HdTextureHandle texture_tracker_get_hd_texture_index(struct TextureTracker *self, Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable_out, bool &cache_hit) {
		fastpath_capable_out = false;
		Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode_Palette8bpp ? 256 : 16, 1);

		// TODO: I'm pretty sure this doesn't handle TextureMode_ABGR1555

		uint32_t palette_hash = 0;
		cache_hit = false;
		if (self->hd_textures_enabled || self->dump_enabled) {
			if (mode.mode == TextureMode_Palette8bpp || mode.mode == TextureMode_Palette4bpp) {
				palette_hash = texture_tracker_get_palette_hash(self, palette_rect);
			}
		}
		if (self->hd_textures_enabled) {
			// Check if the same texture as last time is used.
			HandleCacheResult cache_result = self->handle_cache.get(rect, palette_hash);
			cache_hit = cache_result.found;
			if (cache_hit) {
				// cache_result.handle is currently always a non-fused, non-none, index + palette_hash
				// in the future it may be useful to cache none, but there's currently no way to check if such a containing rect is still alive (since HdTextureHandle's index would be -1)
				EnduringTextureRect &tex = self->tracker.textures.a[cache_result.handle.index]; // Forgive me
				if (tex.alive) {
					fastpath_capable_out = self->fastpath_enabled && ((hd_tex_map_find(&tex.texture_rect.upload->textures, palette_hash) ? hd_tex_map_find(&tex.texture_rect.upload->textures, palette_hash)->alpha_flags : 0) & ALPHA_FLAG_TRANSPARENT) == 0;
					return cache_result.handle;
				}
			}
		}

		static RectIndexSet overlap = { NULL, 0, 0 };
		rect_tracker_overlapping(&self->tracker, rect, &overlap);

		// Dump texture
		for (int oi = 0; oi < overlap.count; oi++) {
			RectIndex index = overlap.items[oi];
			TextureRect *tex = rect_tracker_get_index(&self->tracker, index);
			texture_tracker_dump_texture(self, tex->upload, mode, { mode.mode, palette_hash });
		}
		if (self->frame_dump != NULL) {
			if (self->frame_dump_need_comma) {
				filestream_printf(self->frame_dump, ",");
			} else {
				self->frame_dump_need_comma = true;
			}
			filestream_printf(self->frame_dump, " { \"rect\": ");
			output_rect_json(self->frame_dump, rect);
			filestream_printf(self->frame_dump,
					", \"mode\": { \"mode\": %d, \"palette_x\": %u, \"palette_y\": %u} }\n",
					(int)mode.mode, mode.palette_offset_x, mode.palette_offset_y);
		}

		if (!self->hd_textures_enabled) {
			fastpath_capable_out = false;
			return HdTextureHandle::make_none();
		}

		HdTextureHandle result = HdTextureHandle::make_none();

		Rect result_rect;
		for (int oi = 0; oi < overlap.count; oi++) {
			RectIndex index = overlap.items[oi];
			TextureRect *tex = rect_tracker_get_index(&self->tracker, index);
			HdTexEntry *overlapped_image = hd_tex_map_find(&tex->upload->textures, palette_hash);
			if (overlapped_image == NULL) {
				// Not bound to this upload yet. Bind it now: a GPU-cache hit binds
				// in-frame (handle copy, used by THIS draw - no 1-self->frame native
				// flicker), otherwise this schedules a decode / GPU upload that
				// lands on a later self->frame.
				texture_tracker_request_hd_texture(self, tex->upload, palette_hash);
				overlapped_image = hd_tex_map_find(&tex->upload->textures, palette_hash);
			}
			if (overlapped_image != NULL) {
				if (result == HdTextureHandle::make_none()) {
					// note that if tex->vram_rect contains rect, then it will be the only entry in overlap, so an early out would be pointless
					result_rect = fromSRect(tex->vram_rect);
					fastpath_capable_out = self->fastpath_enabled && fromSRect(tex->vram_rect).contains(rect) && (overlapped_image->alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
					result = HdTextureHandle::make(index, palette_hash);
				} else {
					// Multiple overlap, must fuse
					unsigned int width
						= mode.mode == TextureMode_Palette4bpp ? 64
						: mode.mode == TextureMode_Palette8bpp ? 128
						: 256;
					Rect page_rect = { page_x, page_y, width, 256 };
					fastpath_capable_out = false;
					return fused_pages_get_or_make(&self->fused_pages, page_rect, palette_hash, &self->tracker, self->uploader);
				}
			}
		}

		if (result != HdTextureHandle::make_none())
			self->handle_cache.insert(result_rect, palette_hash, result);
		return result;
	}

	HdTexture texture_tracker_get_hd_texture(struct TextureTracker *self, HdTextureHandle handle)
	{
		if (!handle.fused)
		{
			// HdTextureHandle's are perhaps too tricky.  They assume that the RectTracker's textures vector hasn't removed anything since the handle was
			// created. So it would seem all you need to do is, in Renderer::reset_queue, call RectTracker::releaseDeadHandles. Except you have to be
			// very very careful that no handles outside of the queues (ie. local) exist across a call to reset_queue.  That is, the handle must go into
			// the queue as soon as possible, otherwise that hd texture might not work (previously it would segfault).
			TextureRect *tex = rect_tracker_get_index(&self->tracker, handle.index);
			if (tex == NULL) {
				if (handle.index != -1) {
					TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
				}
				return {
					{0, 0, 1, 1},
					{0, 0, int(image_get_width(ih_get(&self->default_hd_texture), 0)), int(image_get_height(ih_get(&self->default_hd_texture), 0))},
					self->default_hd_texture
				};
			}
			TextureUpload &upload = *tex->upload;
			// Use find rather than index, because if a stale HdTextureHandle was provided this could segfault
			// because indexing on a key that isn't present would initialize a new one with a null pointer
			HdTexEntry *iter = hd_tex_map_find(&upload.textures, handle.palette_hash);
			if (iter == NULL) {
				TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
				return {
					{0, 0, 1, 1},
					{0, 0, int(image_get_width(ih_get(&self->default_hd_texture), 0)), int(image_get_height(ih_get(&self->default_hd_texture), 0))},
					self->default_hd_texture
				};
			}
			/* Reconstruct a counted handle from the stored raw image (add_reference
			 * then adopt), matching hd_gpu_image_handle. */
			image_add_reference(iter->image);
			ImageHandle image = ih_make(iter->image);
			int scaleX = image_get_width(ih_get(&image), 0) / upload.width;
			int scaleY = image_get_height(ih_get(&image), 0) / upload.height;
			SRect texture_subrect = tex->texture_subrect();
			return {
				tex->vram_rect,
				{
					texture_subrect.x * scaleX,
					texture_subrect.y * scaleY,
					texture_subrect.width * scaleX,
					texture_subrect.height * scaleY
				},
				image
			};
		}
		else
			return fused_pages_get_from_handle(&self->fused_pages, handle, &self->default_hd_texture);
	}

	static inline bool is_power_of_two(int n) {
		// https://stackoverflow.com/questions/108318/whats-the-simplest-way-to-test-whether-a-number-is-a-power-of-2-in-c
		return n != 0 && (n & (n - 1)) == 0;
	}

	// TEMPORARY:
	void texture_tracker_on_queues_reset(struct TextureTracker *self) {
		self->handle_cache.clear();
		rect_tracker_releaseDeadHandles(&self->tracker); // This is called from reset_queue, so as of now no HdTextureHandle's exist

		// Poll HD uploads

		slock_lock(self->iothread.channel->lock);
		IOResponse *responses = io_channel_take_responses(self->iothread.channel); // steal the list
		slock_unlock(self->iothread.channel->lock);

		// Move freshly decoded images into the cache (decode-once); mark them for
		// attach. The cache owns them regardless of whether their hash is resident.
		// Each response node is freed after its levels are moved into the cache.
		{
			IOResponse *response = responses;
			while (response != NULL) {
				IOResponse *rnext = response->next;
				HdTextureId id;
				self->dbg_responses_received++;
				id.hash = response->hash;
				id.palette_hash = response->palette_hash;
				hd_key_set_erase(&self->requested, hd_pack_key(id)); // no longer in flight; now cached
				hd_image_cache_put(&self->hd_cache, id, &response->levels, response->alpha_flags);
				hd_key_set_insert(&self->pending_attach, hd_pack_key(id));
				io_response_free(response); // levels already moved out (now empty)
				response = rnext;
			}
		}

		// Attach pass: for every wanted combo whose base hash is currently
		// resident, bind an HD image to it. Prefer the GPU cache (a ref-counted
		// handle copy - no upload); otherwise build the image from the CPU cache
		// and store it in the GPU cache. Combos whose hash isn't resident yet stay
		// cached (NOT discarded) and attach on a later self->frame.
		{
			int pi;
			for (pi = 0; pi < self->pending_attach.count; pi++) {
				HdTextureId id;
				id.hash = (uint32_t)(self->pending_attach.keys[pi] >> 32);
				id.palette_hash = (uint32_t)self->pending_attach.keys[pi];
				TextureUpload *upload = texture_tracker_find_upload(self, id.hash); /* borrowed */
				if (upload == NULL)
					continue; // not resident yet; kept in cache
				if (hd_tex_map_contains(&upload->textures, id.palette_hash))
					continue; // already attached

				// Tier 1: ready-to-bind GPU image - just copy the handle.
				CachedGpuImage *gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(id));
				if (gpu != NULL) {
					hd_tex_map_set(&upload->textures, id.palette_hash, gpu->image, gpu->alpha_flags);
					self->dbg_attaches++;
					for (EnduringTextureRect &e : self->tracker.textures)
					{
						if (e.alive && e.texture_rect.upload == upload)
							fused_pages_mark_dirty(&self->fused_pages, fromSRect(e.texture_rect.vram_rect));
					}
					continue;
				}

				// Tier 2: decoded CPU levels - upload to GPU, then cache the image.
				CachedHdImage *cached = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
				if (cached == NULL)
					continue; // evicted from both caches; will be re-self->requested on draw

				int width = cached->levels.levels[0].width;
				int height = cached->levels.levels[0].height;
				if (width  % upload->width  == 0 && is_power_of_two(width  / upload->width) &&
						height % upload->height == 0 && is_power_of_two(height / upload->height))
				{
					ImageHandle texture = self->uploader->upload_texture(cached->levels);
					hd_gpu_cache_put(&self->hd_gpu_cache, id, texture, cached->alpha_flags, cached->bytes);
					hd_tex_map_set(&upload->textures, id.palette_hash, ih_get(&texture), cached->alpha_flags);
					/* Both caches above retain their own reference; drop the
					 * upload_texture producer reference held by this local. */
					ih_reset(&texture);
					self->dbg_gpu_uploads++;
					self->dbg_attaches++;
					for (EnduringTextureRect &e : self->tracker.textures) {
						if (e.alive && e.texture_rect.upload == upload)
							fused_pages_mark_dirty(&self->fused_pages, fromSRect(e.texture_rect.vram_rect));
					}
				} else {
					TT_LOG(RETRO_LOG_WARN, "Dimension mismatch for %x-%x, original=%dx%d, replacement=%dx%d\n",
							id.hash, id.palette_hash, upload->width, upload->height, width, height);
					HdImageCache_erase(&self->hd_cache, hd_pack_key(id));    // don't keep a bad-sized image around
					hd_key_set_insert(&self->requested, hd_pack_key(id));  // negatively cache so we don't reload + re-warn every self->frame
				}
			}
		}
		hd_key_set_clear(&self->pending_attach);

		fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker, self->uploader);
		fused_pages_remove_dead(&self->fused_pages);
	}
	TextureUpload *texture_tracker_find_upload(struct TextureTracker *self, uint32_t hash) {
		TextureUpload *upload = rect_tracker_find_upload(&self->tracker, hash); /* borrowed */
		int _ri;

		if (upload != NULL)
			return upload;

		// backup search, in case it's restorable but currently missing from the rect tracker
		for (_ri = 0; _ri < self->restorable_rects.size(); _ri++)
		{
			RestorableRect &entry = self->restorable_rects[_ri];
			size_t _ti;
			for (_ti = 0; _ti < entry.to_restore.size(); _ti++)
			{
				TextureRect &t = entry.to_restore[_ti];
				if (hash == t.upload->hash)
					return t.upload;
			}
		}

		return NULL;
	}

	void texture_tracker_endFrame(struct TextureTracker *self) {
		self->frame += 1;

		if (self->frame % 300 == 0)
		{
			TT_LOG_VERBOSE(RETRO_LOG_INFO, "hit ratio: %f (%ld, %ld)\n", double(self->handle_cache.dbg_hits) / (self->handle_cache.dbg_hits + self->handle_cache.dbg_misses), self->handle_cache.dbg_hits, self->handle_cache.dbg_misses);
			self->handle_cache.dbg_hits = 0;
			self->handle_cache.dbg_misses = 0;
			TT_LOG(RETRO_LOG_INFO, "[hdcache] last 300f: %llu decodes, %llu gpu-uploads, %llu attaches\n",
					(unsigned long long)(self->dbg_responses_received - self->dbg_responses_received_last),
					(unsigned long long)(self->dbg_gpu_uploads - self->dbg_gpu_uploads_last),
					(unsigned long long)(self->dbg_attaches - self->dbg_attaches_last));
			TT_LOG(RETRO_LOG_INFO, "[hdcache] mode=%s ; ram %zu/%zu MB (%zu entries) ; vram %zu/%zu MB (%zu entries)\n",
					self->eager_textures ? "eager" : "lazy",
					HdImageCache_size_bytes(&self->hd_cache) / (1024 * 1024), HdImageCache_budget(&self->hd_cache) / (1024 * 1024), HdImageCache_count(&self->hd_cache),
					HdGpuCache_size_bytes(&self->hd_gpu_cache) / (1024 * 1024), HdGpuCache_budget(&self->hd_gpu_cache) / (1024 * 1024), HdGpuCache_count(&self->hd_gpu_cache));
			self->dbg_responses_received_last = self->dbg_responses_received;
			self->dbg_gpu_uploads_last = self->dbg_gpu_uploads;
			self->dbg_attaches_last = self->dbg_attaches;
		}

		if (self->frame_dump != NULL) {
			filestream_printf(self->frame_dump, "]}\n");
			filestream_close(self->frame_dump);
			self->frame_dump = NULL;
		}

		if (dbg_input_state_cb != 0)
		{
			if (self->frame_dump_key.query())
			{
				char fdpath[PATH_MAX_TT];
				dump_path(fdpath, sizeof(fdpath));
				snprintf(fdpath + strlen(fdpath), sizeof(fdpath) - strlen(fdpath), "test_dump.json");
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Left bracket!\n");
				self->frame_dump = filestream_open(fdpath, RETRO_VFS_FILE_ACCESS_WRITE,
						RETRO_VFS_FILE_ACCESS_HINT_NONE);
				self->frame_dump_need_comma = false;
				if (self->frame_dump != NULL)
				{
					bool need_comma = false;
					int _eti;
					filestream_printf(self->frame_dump, "{ \"initial\": [\n");
					for (_eti = 0; _eti < self->tracker.textures.count; _eti++)
					{
						EnduringTextureRect *etexture = &self->tracker.textures.a[_eti];
						Rect rect;
						TextureRect *texture;
						if (!etexture->alive) continue;
						texture = &etexture->texture_rect;
						if (need_comma)
							filestream_printf(self->frame_dump, ",");
						else
							need_comma = true;
						filestream_printf(self->frame_dump, " { \"rect\": ");
						rect = fromSRect(texture->vram_rect);
						output_rect_json(self->frame_dump, rect);
						filestream_printf(self->frame_dump, ", \"hash\": \"%x\" }\n", texture->upload->hash);
					}
					filestream_printf(self->frame_dump, "], \"events\": [\n");
				}
			}

			if (self->hd_toggle_key.query()) {
				self->hd_textures_enabled = !self->hd_textures_enabled;
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling hd textures: %s\n", self->hd_textures_enabled ? "on" : "off");
			}

			if (self->reload_key.query()) {
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Reloading hd textures from disk\n");
				texture_tracker_reload_textures_from_disk(self);
			}

			if (self->fastpath_key.query()) {
				self->fastpath_enabled = !self->fastpath_enabled;
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling fastpath %s\n", self->fastpath_enabled ? "ON" : "OFF");
			}
		}
	}

	void texture_tracker_set_texture_uploader(struct TextureTracker *self, Renderer *t) {
		self->uploader = t;
		LoadedLevels default_levels;
		LoadedImage default_image;
		loaded_levels_init(&default_levels);
		loaded_image_init(&default_image);
		loaded_image_alloc(&default_image, 1, 1);
		default_image.owned_data[0] = 0;
		default_image.owned_data[1] = 0;
		default_image.owned_data[2] = 0;
		default_image.owned_data[3] = 0;
		loaded_levels_push_move(&default_levels, &default_image);

		ih_move(&self->default_hd_texture, self->uploader->upload_texture(default_levels));
	}

	void texture_tracker_reload_textures_from_disk(struct TextureTracker *self) {
		char rpath[PATH_MAX_TT];
		// Reload the directory listing
		read_texture_directory(&self->known_files, replacements_path(rpath, sizeof(rpath)));
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found %d hd textures\n", (int)self->known_files.count);

		// Drop all cached / loaded HD state so edited files on disk take effect.
		HdGpuCache_clear(&self->hd_gpu_cache);
		HdImageCache_clear(&self->hd_cache);
		hd_key_set_clear(&self->requested);
		hd_key_set_clear(&self->pending_attach);
		for (EnduringTextureRect &texture : self->tracker.textures)
			hd_tex_map_clear(&texture.texture_rect.upload->textures);
		for (RestorableRect &restorable : self->restorable_rects)
		{
			for (TextureRect &tr : restorable.to_restore)
				hd_tex_map_clear(&tr.upload->textures);
		}

		// Delete fused textures
		fused_pages_mark_dead(&self->fused_pages, {0, 0, FB_WIDTH, FB_HEIGHT});

		// Draws will lazily re-request and the cache repopulates.
	}

	// RectTracker

	bool intersects(SRect a, SRect b) {
		return !(
				a.left() >= b.right() ||
				b.left() >= a.right() ||
				a.top() >= b.bottom() ||
				b.top() >= a.bottom()
			);
	}

	SRect bounds(int left, int right, int top, int bottom) {
		return SRect(left, top, right - left, bottom - top);
	}

	static void split(SRect original, SRect remove, SRect *results, unsigned &count)
	{
		SRectResult intersectionResult = intersect(original, remove);
		if (!intersectionResult.valid)
		{
			results[count++] = original;
			return;
		}

		SRect intersection = intersectionResult.rect;

		// Top rect
		if (intersection.top() > original.top()) {
			results[count++] = bounds(
					original.left(),
					original.right(),
					original.top(),
					intersection.top()
					);
		}

		// Bottom rect
		if (intersection.bottom() < original.bottom()) {
			results[count++] = bounds(
					original.left(),
					original.right(),
					intersection.bottom(),
					original.bottom()
					);
		}

		// Left rect
		if (intersection.left() > original.left()) {
			results[count++] = bounds(
					original.left(),
					intersection.left(),
					intersection.top(),
					intersection.bottom()
					);
		}

		// Right rect
		if (intersection.right() < original.right()) {
			results[count++] = bounds(
					intersection.right(),
					original.right(),
					intersection.top(),
					intersection.bottom()
					);
		}
	}

	void rect_tracker_upload(struct RectTracker *self, SRect rect, TextureUpload *upload) {
		TextureRect texture = make_texture_rect(upload, 0, 0, rect);
		rect_tracker_place(self, texture);
		self->lookup_grid_dirty = true;
	}

	SRect moved(SRect rect, int dx, int dy) {
		return SRect(rect.x + dx, rect.y + dy, rect.width, rect.height);
	}

	void rect_tracker_blit(struct RectTracker *self, SRect dst, SRect src) {
		TextureRectVec to_place = { NULL, 0, 0 };
		int moveX = dst.x - src.x;
		int moveY = dst.y - src.y;
		int _ti;
		for (_ti = 0; _ti < self->textures.count; _ti++) {
			EnduringTextureRect *eold = &self->textures.a[_ti];
			if (eold->alive) {
				TextureRect *old = &eold->texture_rect;
				SRectResult intersection = intersect(old->vram_rect, src);
				if (intersection.valid) {
					TextureRect sub = subTexture(*old, intersection.rect);
					TextureRect subMoved = make_texture_rect(sub.upload, sub.offset_x, sub.offset_y, moved(sub.vram_rect, moveX, moveY));
					TextureRectVec_push(&to_place, &subMoved);
				}
			}
		}
		rect_tracker_clear_rect(self, &dst);
		{
			int _i;
			for (_i = 0; _i < TextureRectVec_size(&to_place); _i++)
				rect_tracker_place(self, *TextureRectVec_at(&to_place, _i));
		}
		TextureRectVec_free_storage(&to_place);
		self->lookup_grid_dirty = true;
	}

	void rect_tracker_releaseDeadHandles(struct RectTracker *self)
	{
		enduring_arr_compact(&self->textures);
		self->lookup_grid_dirty = true;
	}

	RectIndexSet *rect_tracker_overlapping(struct RectTracker *self, Rect uvrect, RectIndexSet *results)
	{
		SRect rect;
		if (self->lookup_grid_dirty)
			rect_tracker_rebuild_lookup_grid(self);

		// TODO: remove this when renderer/build_attribs doesn't 
		// have an unnecessary - 1
		if (uvrect.width == 0)
			uvrect.width = 1;

		rect = toSRect(uvrect);

		rect_index_set_clear(results);
		lookup_grid_get(&self->lookup_grid, rect, results);
		return results;
	}

	TextureRect *rect_tracker_get_index(struct RectTracker *self, RectIndex index)
	{
		if (index < 0 || index >= self->textures.count)
			return NULL;
		return &self->textures.a[index].texture_rect;
	}

	static void rect_tracker_clear_rect(struct RectTracker *self, SRect *rect) {
		SRect splits[4];
		unsigned splits_count = 0;
		int ti, ni;

		TextureRectVec newTextures = { NULL, 0, 0 };
		for (ti = 0; ti < self->textures.count; ti++) {
			EnduringTextureRect *eold = &self->textures.a[ti];
			if (eold->alive) {
				TextureRect *old = &eold->texture_rect;

				splits_count = 0;
				split(old->vram_rect, *rect, splits, splits_count);
				// The rect didn't split, do nothing
				if (splits_count == 1 && splits[0] == old->vram_rect) { }
				else
				{
					// The rect split, mark this texture as dead and push its splits to be added
					unsigned i;
					eold->alive = false;
					for (i = 0; i < splits_count; i++)
						{ TextureRect _tr = subTexture(*old, splits[i]); TextureRectVec_push(&newTextures, &_tr); }
				}
			}
		}
		for (ni = 0; ni < newTextures.count; ni++)
			enduring_arr_push(&self->textures, newTextures.items[ni], true);
		TextureRectVec_free_storage(&newTextures);
	}
	void rect_tracker_place(struct RectTracker *self, TextureRect texture) {
		rect_tracker_clear_rect(self, &texture.vram_rect);
		enduring_arr_push(&self->textures, texture, true);
	}

	static void rect_tracker_rebuild_lookup_grid(struct RectTracker *self) {
		int i;
		lookup_grid_clear(&self->lookup_grid);
		for (i = 0; i < self->textures.count; i++)
		{
			if (self->textures.a[i].alive)
				lookup_grid_insert(&self->lookup_grid, self->textures.a[i].texture_rect.vram_rect, i);
		}
		self->lookup_grid_dirty = false;
	}

	TextureUpload *rect_tracker_find_upload(struct RectTracker *self, uint32_t hash) {
		int i;
		for (i = 0; i < self->textures.count; i++)
		{
			EnduringTextureRect *eold = &self->textures.a[i];
			if (eold->texture_rect.upload->hash == hash)
				return eold->texture_rect.upload;
		}
		return NULL;
	}

	static inline int clamp(int x, int low, int high)
	{
		if (x < low)  x = low;
		if (x > high) x = high;
		return x;
	}

	struct CellBounds
	{
		int lowX;
		int highX; // exclusive
		int lowY;
		int highY; // exclusive
	};

	CellBounds cellBounds(SRect vram) {
		return CellBounds({
				clamp(vram.left() / LOOKUP_CELL_WIDTH, 0, LOOKUP_GRID_COLUMNS),
				clamp(ceil(vram.right() / float(LOOKUP_CELL_WIDTH)), 0, LOOKUP_GRID_COLUMNS),
				clamp(vram.top() / LOOKUP_CELL_HEIGHT, 0, LOOKUP_GRID_ROWS),
				clamp(ceil(vram.bottom() / float(LOOKUP_CELL_HEIGHT)), 0, LOOKUP_GRID_ROWS)
				});
	}

	void lookup_grid_init(LookupGrid *g) {
		int i;
		for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++) {
			g->cells[i].entries = NULL;
			g->cells[i].count = 0;
			g->cells[i].cap = 0;
		}
	}

	void lookup_grid_deinit(LookupGrid *g) {
		int i;
		for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++)
			free(g->cells[i].entries);
	}

	void lookup_grid_insert(LookupGrid *self, SRect r, RectIndex index)
	{
		CellBounds c = cellBounds(r);
		int x, y;
		for (x = c.lowX; x < c.highX; x++) {
			for (y = c.lowY; y < c.highY; y++) {
				LookupGrid::Cell *cell = &self->cells[y * LOOKUP_GRID_COLUMNS + x];
				if (cell->count == cell->cap) {
					int ncap = cell->cap ? cell->cap * 2 : 8;
					LookupGrid::LookupEntry *ne = (LookupGrid::LookupEntry *)realloc(cell->entries, (size_t)ncap * sizeof(LookupGrid::LookupEntry));
					if (!ne)
						return;
					cell->entries = ne;
					cell->cap = ncap;
				}
				cell->entries[cell->count].rect = r;
				cell->entries[cell->count].index = index;
				cell->count++;
			}
		}
	}

	void lookup_grid_get(LookupGrid *self, SRect r, RectIndexSet *results)
	{
		CellBounds c = cellBounds(r);
		int x, y;
		for (x = c.lowX; x < c.highX; x++)
		{
			for (y = c.lowY; y < c.highY; y++)
			{
				LookupGrid::Cell *cell = &self->cells[y * LOOKUP_GRID_COLUMNS + x];
				int e;
				for (e = 0; e < cell->count; e++)
				{
					if (intersects(cell->entries[e].rect, r))
						rect_index_set_insert(results, cell->entries[e].index);
				}
			}
		}
	}
	void lookup_grid_clear(LookupGrid *self)
	{
		int i;
		for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++)
			self->cells[i].count = 0; /* keep allocation for reuse */
	}

	// FusedPages

	static inline int64_t page_bytes(FusionRects &fusion)
	{
		return fusion.scaleX * fusion.scaleY * fusion.vram_rect.width * fusion.vram_rect.height * 4;
	}

	void fused_pages_dbg_print_info(struct FusedPages *self) {
		int64_t num_bytes = 0;
		int _i;
		for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
			num_bytes += page_bytes(fused_page_vec_at(&self->pages, _i)->fusion);
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Fused Pages: %lu, Bytes: %ld (%.1f MiB)\n", (unsigned long)fused_page_vec_size(&self->pages), num_bytes, num_bytes / 1048576.0);
	}

	static bool srect_gt(const SRect &a, const SRect &b)
	{
		if (a.x != b.x)
			return a.x > b.x;
		if (a.y != b.y)
			return a.y > b.y;
		if (a.width != b.width)
			return a.width > b.width;
		return a.height > b.height;
	}

	static bool texture_rect_sort_gt(const TextureRect &a, const TextureRect &b) {
		// Compare .upload by pointer
		if (a.upload != b.upload)
			return a.upload > b.upload;
		if (a.vram_rect != b.vram_rect)
			return srect_gt(a.vram_rect, b.vram_rect);
		return srect_gt(a.texture_subrect(), b.texture_subrect());
	}

	/* qsort comparator: descending order, matching texture_rect_sort_gt. Equal
	 * elements (fully identical under the predicate) are interchangeable, so the
	 * unstable order among them does not affect the canonical-form comparison this
	 * sort exists to enable. */
	static int texture_rect_qsort_cmp(const void *pa, const void *pb) {
		const TextureRect &a = *(const TextureRect *)(pa);
		const TextureRect &b = *(const TextureRect *)(pb);
		if (texture_rect_sort_gt(a, b))
			return -1;
		if (texture_rect_sort_gt(b, a))
			return 1;
		return 0;
	}

	FusionRects fusion_rects(Rect full_page_rect, uint32_t palette_hash, struct RectTracker *tracker) {
		FusionRects f;
		f.scaleX = 0;
		f.scaleY = 0;
		f.vram_rect = {0, 0, 0, 0};

		int _ei;
		for (_ei = 0; _ei < tracker->textures.count; _ei++) {
			EnduringTextureRect *e = &tracker->textures.a[_ei];
			SRectResult intersection;
			if (!e->alive)
				continue;
			intersection = intersect(toSRect(full_page_rect), e->texture_rect.vram_rect);
			if (intersection.valid) {
				TextureUpload *upload = e->texture_rect.upload;
				HdTexEntry *hd_texture = hd_tex_map_find(&upload->textures, palette_hash);
				if (hd_texture != NULL) {
					// Clip to the destination texture (important, otherwise it might blit out of bounds which may have wrought havoc upon my sanity)
					TextureRect clipped = subTexture(e->texture_rect, intersection.rect);
					unsigned hd_scale_x = image_get_width(hd_texture->image, 0) / upload->width;
					unsigned hd_scale_y = image_get_height(hd_texture->image, 0) / upload->height;
					f.scaleX = max_(f.scaleX, hd_scale_x);
					f.scaleY = max_(f.scaleY, hd_scale_y);
					Rect r = fromSRect(clipped.vram_rect);
					if (f.vram_rect.width == 0)
						f.vram_rect = r;
					else
						f.vram_rect.extend_bounding_box(r);
					f.rects.push(clipped);
				}
			}
		}

		// Sort rects so that the vector itself can be compared
		qsort(TextureRectVec_data(&f.rects.v), TextureRectVec_size(&f.rects.v), sizeof(TextureRect), texture_rect_qsort_cmp);

		return f;
	}

	void rebuild_page(FusedPage &page, struct RectTracker *tracker, Renderer *uploader) {
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilding page for %x, %d,%d %dx%d\n",
				page.palette,
				page.fusion.vram_rect.x,
				page.fusion.vram_rect.y,
				page.fusion.vram_rect.width,
				page.fusion.vram_rect.height
			      );

		page.dirty = false;

		{
			FusionRects fusion = fusion_rects(page.full_page_rect, page.palette, tracker);
			if (page.fusion == fusion) {
				TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: no change\n");
				return;
			}
			page.fusion = static_cast<FusionRects &&>(fusion);
		}

		if (page.fusion.rects.size() == 0) {
			page.dead = true;
			ih_reset(&page.texture);
			TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page is now dead\n");
			return;
		}

		CommandBufferHandle &cmd = uploader->command_buffer_hack_fixme();

		int texture_width = page.fusion.vram_rect.width * page.fusion.scaleX;
		int texture_height = page.fusion.vram_rect.height * page.fusion.scaleY;

		// TODO: I don't know SHIT about barriers.

		// special sentinel value
		// Note that due to the way textures are put into a page, these special values will not bleed into neighbors in the mipmaps,
		// because the mipmaps are only used down to the original resolution, and hd textures are aligned to that original resolution's
		// texels.
		VkClearValue fallthrough = {0.0, 0.0, 0.0, 1.0};

		int mip_levels = log2(min_(page.fusion.scaleX, page.fusion.scaleY)) + 1;

		if (ih_is_valid(&page.texture) && image_get_width(ih_get(&page.texture), 0) == texture_width && image_get_height(ih_get(&page.texture), 0) == texture_height)
			// Switch back into transfer dst layout
			commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&page.texture),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
		else
			ih_move(&page.texture, uploader->create_texture(texture_width, texture_height, mip_levels));
		commandbuffer_clear_image(cbh_get(&cmd), *ih_get(&page.texture), fallthrough);

		// Second pass to blit all the existing textures into the new texture
		for (TextureRect &tex : page.fusion.rects) {
			TextureUpload &upload = *tex.upload;

			HdTexEntry *hd_texture = hd_tex_map_find(&upload.textures, page.palette);
			// That's odd
			if (hd_texture == NULL)
				continue;

			Image *image = hd_texture->image;

			int srcWidth = image_get_width(image, 0);
			int srcHeight = image_get_height(image, 0);

			int sx = srcWidth / upload.width;
			int sy = srcHeight / upload.height;

			int rx = page.fusion.scaleX / sx;
			int ry = page.fusion.scaleY / sy;

			SRect subrect = tex.texture_subrect();

			VkOffset3D dst_offset = {
				(tex.vram_rect.x - int(page.fusion.vram_rect.x)) * int(page.fusion.scaleX),
				(tex.vram_rect.y - int(page.fusion.vram_rect.y)) * int(page.fusion.scaleY),
				0
			};
			VkOffset3D dst_extent = {
				tex.vram_rect.width * int(page.fusion.scaleX),
				tex.vram_rect.height * int(page.fusion.scaleY),
				1
			};

			// Switch into transfer src
			// what the fuck am I doing?
			commandbuffer_image_barrier(cbh_get(&cmd), 
					*image,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
					0,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					0
					);

			// Blit into every mipmap level down to base vram
			// TODO: this isn't a great way to do this, will probably be blurrier than it could be if src and dst aspect ratios are different
			// TODO: is this line even right? it sure doesn't look right
			int full_res_levels = log2(max_(rx, ry)) + 1;
			// assert(max_level >= 0 && max_level <= 6);
			// TODO: this is incredibly finicky, and one bad (out of bounds) blit can bork everything
			for (int dstLevel = 0; dstLevel < mip_levels; dstLevel++) {
				int srcLevel = max_(0, dstLevel - full_res_levels);

				commandbuffer_blit_image(cbh_get(&cmd), *ih_get(&page.texture), *image,
						dst_offset,
						dst_extent,
						{
						(sx * subrect.x) >> srcLevel,
						(sy * subrect.y) >> srcLevel,
						0
						},
						{ 
						(sx * subrect.width) >> srcLevel,
						(sy * subrect.height) >> srcLevel,
						1
						},
						dstLevel,
						srcLevel,
						0, 0, 1, VK_FILTER_LINEAR
					       );

				dst_offset.x >>= 1;
				dst_offset.y >>= 1;
				dst_extent.x = max_(dst_extent.x >> 1, 1);
				dst_extent.y = max_(dst_extent.y >> 1, 1);
			}

			// Change back to shader read
			commandbuffer_image_barrier(cbh_get(&cmd), 
					*image,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
					0,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					0
					);
		}

		// I have no idea what the fuck I'm doing
		// Make the fused texture readable by shaders
		commandbuffer_image_barrier(cbh_get(&cmd), *ih_get(&page.texture),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page now %ux%u, %ld bytes (%.1f MiB)\n",
				page.fusion.vram_rect.width * page.fusion.scaleX, page.fusion.vram_rect.height * page.fusion.scaleY,
				page_bytes(page.fusion), page_bytes(page.fusion) / 1048576.0
			      );
	}

	HdTexture fused_pages_get_from_handle(struct FusedPages *self, HdTextureHandle handle, ImageHandle *default_hd_texture) {
		FusedPage *page;
		if (handle.index < 0 || handle.index >= fused_page_vec_size(&self->pages)) {
			TT_LOG(RETRO_LOG_WARN, "BAD fused index!\n");
			HdTexture _r;
			_r.vram_rect = (SRect){0, 0, 1, 1};
			_r.texel_rect = (SRect){0, 0, int(image_get_width(ih_get(default_hd_texture), 0)), int(image_get_height(ih_get(default_hd_texture), 0))};
			_r.texture = *default_hd_texture;
			return _r;
		}
		page = fused_page_vec_at(&self->pages, handle.index);
		if (!ih_is_valid(&page->texture)) {
			HdTexture _r;
			TT_LOG(RETRO_LOG_WARN, "Missing fused texture!\n");
			_r.vram_rect = (SRect){0, 0, 1, 1};
			_r.texel_rect = (SRect){0, 0, int(image_get_width(ih_get(default_hd_texture), 0)), int(image_get_height(ih_get(default_hd_texture), 0))};
			_r.texture = *default_hd_texture;
			return _r;
		}
		{
			HdTexture _r;
			_r.vram_rect = toSRect(page->fusion.vram_rect);
			_r.texel_rect = (SRect){ 0, 0, int(image_get_width(ih_get(&page->texture), 0)), int(image_get_height(ih_get(&page->texture), 0)) };
			_r.texture = page->texture;
			return _r;
		}
	}

	HdTextureHandle fused_pages_get_or_make(struct FusedPages *self, Rect page_rect, uint32_t palette, struct RectTracker *tracker, Renderer *uploader) {
		int x;
		FusedPage page;
		for (x = 0; x < fused_page_vec_size(&self->pages); x++)
		{
			FusedPage *p = fused_page_vec_at(&self->pages, x);
			// return page
			if (!p->dead && p->palette == palette && p->full_page_rect == page_rect)
				return HdTextureHandle::make_fused(x);
		}

		// Make a new fused page
		TT_LOG_VERBOSE(RETRO_LOG_INFO, "Creating new fused page for palette %x\n", palette);

		page.dead = false;
		page.dirty = false;
		page.full_page_rect = page_rect;
		page.palette = palette;
		rebuild_page(page, tracker, uploader);
		fused_page_vec_push(&self->pages, &page);
		return HdTextureHandle::make_fused(fused_page_vec_size(&self->pages) - 1);
	}
	void fused_pages_mark_dirty(struct FusedPages *self, Rect rect) {
		int _i;
		for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
		{
			FusedPage *page = fused_page_vec_at(&self->pages, _i);
			if (!page->dead && page->full_page_rect.intersects(rect))
				page->dirty = true;
		}
	}
	void fused_pages_mark_dead(struct FusedPages *self, Rect rect) {
		int _i;
		for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
		{
			FusedPage *page = fused_page_vec_at(&self->pages, _i);
			if (!page->dead && page->full_page_rect.intersects(rect))
				page->dead = true;
		}
	}
	void fused_pages_rebuild_dirty(struct FusedPages *self, struct RectTracker *tracker, Renderer *uploader) {
		bool changed = false;
		int _i;
		for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++) {
			FusedPage *page = fused_page_vec_at(&self->pages, _i);
			if (!page->dead && page->dirty) {
				rebuild_page(*page, tracker, uploader);
				changed = true;
			}
		}
		if (changed)
			fused_pages_dbg_print_info(self);
	}
	void fused_pages_remove_dead(struct FusedPages *self) {
		int retained = 0;
		int i;
		for (i = 0; i < fused_page_vec_size(&self->pages); i++) {
			if (!fused_page_vec_at(&self->pages, i)->dead) {
				if (retained != i) {
					FusedPage *dst = fused_page_vec_at(&self->pages, retained);
					FusedPage *src = fused_page_vec_at(&self->pages, i);
					fp_destroy(dst);
					fp_copy(dst, src);
				}
				retained++;
			}
		}
		fused_page_vec_truncate(&self->pages, retained);
	}


	//========================================
	// Save State

	/* Allocate a new upload (refcount 1) that is a deep copy of to_copy with the
	 * HD textures map cleared. Replaces the old by-value copy ctor flow. */
	TextureUpload *texture_upload_new_copy_without_handles(const TextureUpload *to_copy) {
		TextureUpload *copy = texture_upload_new();
		texture_upload_copy_contents(copy, to_copy);
		hd_tex_map_clear(&copy->textures);
		return copy;
	}

	/* Transient hash -> upload-pointer lookup used only while loading a save
	 * state. Non-owning (the pointers are owned by the +1 ref taken in
	 * load_state and dropped at the end), keys are unique and inserted once, so
	 * a flat POD array with a linear scan replaces the old
	 * std::map<uint32_t, TextureUpload*>. */
	struct UploadPtrEntry { uint32_t key; TextureUpload *val; };
	POD_VEC_DECLARE(UploadPtrVec, UploadPtrEntry);

	TextureRectSaveState to_save_state(const TextureRect &t, UploadOwningMap &uploads) {
		uint32_t hash = t.upload->hash;
		if (!uploads.contains(hash))
			uploads.insert(hash, texture_upload_new_copy_without_handles(t.upload));
		return {
			t.upload->hash,
			t.offset_x,
			t.offset_y,
			t.vram_rect
		};
	}

	TextureRect from_save_state(const TextureRectSaveState &t, UploadPtrVec &uploads) {
		TextureUpload *found = NULL;
		for (int i = 0; i < uploads.count; i++) {
			if (uploads.items[i].key == t.upload_hash) {
				found = uploads.items[i].val;
				break;
			}
		}
		if (!found) {
			TT_LOG(RETRO_LOG_ERROR, "SaveState upload missing!\n");
		}
		return {
			found,    /* TextureRect ctor acquires its own ref */
			t.offset_x,
			t.offset_y,
			t.vram_rect
		};
	}

	TextureTrackerSaveState texture_tracker_save_state(struct TextureTracker *self)
	{
		TextureTrackerSaveState state;

		for (EnduringTextureRect &r : self->tracker.textures)
		{
			if (r.alive)
			{
				TextureRectSaveState _ss = to_save_state(r.texture_rect, state.uploads);
				TextureRectSaveStateVec_push(&state.rects, &_ss);
			}
		}
		for (RestorableRect &r : self->restorable_rects)
		{
			RestorableRectSaveState saved;
			rrss_init(&saved);
			saved.hash = r.hash;
			saved.rect = r.rect;
			for (TextureRect &t : r.to_restore)
			{
				TextureRectSaveState _ss = to_save_state(t, state.uploads);
				TextureRectSaveStateVec_push(&saved.to_restore, &_ss);
			}
			RestorableRectSaveStateVec_push_move(&state.restorable, &saved);
		}

		return state;
	}


	void texture_tracker_load_state(struct TextureTracker *self, const TextureTrackerSaveState &state)
	{
		UploadPtrVec uploads = { NULL, 0, 0 };
		for (int e = 0; e < state.uploads.count; e++) {
			TextureUpload *ptr = texture_upload_new(); /* owns +1 */
			texture_upload_copy_contents(ptr, state.uploads.items[e].val); /* deep-copy contents (refcount untouched) */
			UploadPtrEntry pe = { state.uploads.items[e].key, ptr };
			UploadPtrVec_push(&uploads, &pe);
		}

		texture_tracker_clearRegion(self, { 0, 0, FB_WIDTH, FB_HEIGHT });
		enduring_arr_clear(&self->tracker.textures); // load_state should only be called right after creating this TextureTracker, so this ought to be empty already anyway
		{
			int _i;
			for (_i = 0; _i < TextureRectSaveStateVec_size(&state.rects); _i++)
				rect_tracker_place(&self->tracker, from_save_state(*TextureRectSaveStateVec_at((struct TextureRectSaveStateVec *)&state.rects, _i), uploads));
		}
		self->restorable_rects.clear();
		{
			int _i;
			for (_i = 0; _i < RestorableRectSaveStateVec_size(&state.restorable); _i++)
			{
				RestorableRectSaveState *r = RestorableRectSaveStateVec_at((struct RestorableRectSaveStateVec *)&state.restorable, _i);
				RestorableRect loaded;
				loaded.hash = r->hash;
				loaded.rect = r->rect;
				int _j;
				for (_j = 0; _j < TextureRectSaveStateVec_size(&r->to_restore); _j++)
					loaded.to_restore.push(from_save_state(*TextureRectSaveStateVec_at(&r->to_restore, _j), uploads));
				self->restorable_rects.push(loaded);
			}
		}
		// Need to reload the hd textures, too
		for (int e = 0; e < state.uploads.count; e++)
			texture_tracker_load_hd_texture(self, state.uploads.items[e].key);
		// Drop the map's construction refs; the placed/restorable TextureRects now
		// hold their own references to each upload.
		for (int i = 0; i < uploads.count; i++)
			texture_upload_release(uploads.items[i].val);
		UploadPtrVec_free_storage(&uploads);
	}
	// End of Save State
	//========================================
	bool DbgHotkey::query()
	{
		uint16_t state = dbg_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, key);
		bool is_key_down = state != 0;
		bool just_pressed = is_key_down && !was_key_down;
		was_key_down = is_key_down;
		return just_pressed;
	}


#include "libretro_vulkan.h"

// #include "mednafen/mednafen.h" is required
// for #include "mednafen/psx/gpu.h" to work properly.
#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"

#include "libretro_cbs.h"
#include "libretro_options.h"
#include "beetle_psx_globals.h"


static Context *context = NULL;
static Device *device = NULL;
static Renderer *renderer = NULL;
static unsigned scaling = 4;

extern enum rhi_renderer_type rhi_type;
extern retro_log_printf_t log_cb;

static retro_hw_render_callback hw_render;
static const struct retro_hw_render_interface_vulkan *vulkan;

/* Owning vector of retro_vulkan_image (POD), replacing
 * std::vector<retro_vulkan_image>. resize() grows/shrinks the backing array;
 * grown slots are zero-initialised. */
struct SwapchainImageVec {
	retro_vulkan_image *items;
	int count;
	int cap;
	void clear() { count = 0; }
	int size() const { return count; }
	retro_vulkan_image &operator[](int i) { return items[i]; }
	void resize(int n) {
		if (n > cap) {
			retro_vulkan_image *ni = (retro_vulkan_image *)realloc(items, (size_t)n * sizeof(retro_vulkan_image));
			if (!ni)
				return;
			items = ni;
			cap = n;
		}
		if (n > count)
			memset(&items[count], 0, (size_t)(n - count) * sizeof(retro_vulkan_image));
		count = n;
	}
	void free_storage() { ::free(items); items = NULL; count = 0; cap = 0; }
};

/* Owning vector of ImageHandle (refcounted), replacing
 * std::vector<ImageHandle>. resize() default-constructs grown slots (NULL
 * handles) and resets/destroys dropped ones; clear() resets every element.
 * operator[] returns a reference so assignment goes through ImageHandle's own
 * release-old/retain-new assignment. */
struct ScanoutHandleVec {
	ImageHandle *items;
	int count;
	int cap;
	void clear() {
		for (int i = 0; i < count; i++)
			ih_reset(&items[i]);
		count = 0;
	}
	int size() const { return count; }
	ImageHandle &operator[](int i) { return items[i]; }
	void resize(int n) {
		for (int i = n; i < count; i++)
			ih_reset(&items[i]);
		if (n > cap) {
			ImageHandle *nitems = (ImageHandle *)malloc((size_t)n * sizeof(ImageHandle));
			for (int i = 0; i < count && i < n; i++) {
				/* Move: ih_steal copies the pointer and nulls the old slot. */
				ih_steal(&nitems[i], &items[i]);
			}
			::free(items);
			items = nitems;
			cap = n;
		}
		for (int i = count; i < n; i++)
			items[i].data = NULL; /* default-construct grown slot to a null handle */
		count = n;
	}
	void free_storage() { clear(); ::free(items); items = NULL; cap = 0; }
};

static SwapchainImageVec swapchain_images;
static ScanoutHandleVec scanout_handles;
static Renderer::SaveState save_state;
static bool inside_frame;
static bool has_software_fb;
static bool scaled_uv_offset;
static int filter_exclude_sprites;
static int filter_exclude_2d_polygons;
static bool adaptive_smoothing;
static bool super_sampling;
static unsigned msaa = 1;
static bool mdec_yuv;
/*
 * Queue for rhi_vulkan_* operations that arrive between the libretro
 * frontend's RETRO_ENVIRONMENT_SET_HW_RENDER acceptance and the
 * frontend invoking vk_context_reset. Drained at the end of
 * vk_context_reset, after the renderer is constructed. This used to be
 * a std::vector<std::function<void()>> with per-entry-point lambdas;
 * it was migrated to the shared C-callable rhi_defer module so the GL
 * backend (a C TU, can't use std::function) can use the same mechanism
 * and so both backends share a single defer policy. See
 * rhi/rhi_defer.h for which ops are deferred and which are dropped.
 */
static rhi_defer_queue_t defer = { NULL, 0, 0 };
static dither_mode dither_mode = DITHER_NATIVE;
static bool dump_textures = false;
static bool replace_textures = false;
static bool track_textures = false;
static size_t hd_cache_vram_bytes = (size_t)3 * 1024 * 1024 * 1024; // 3 GB default (matches HD_CACHE_VRAM_BUDGET)
static size_t hd_cache_ram_bytes  = (size_t)2 * 1024 * 1024 * 1024; // 2 GB default (matches HD_CACHE_RAM_BUDGET)
static bool   eager_hd_textures   = true; // default eager (master-consistent); false = lazy
/*
 * File-local copy of the crop_overscan core option, distinct
 * from the cross-TU `crop_overscan` global declared in
 * beetle_psx_globals.h. Both are populated from the same
 * BEETLE_OPT(crop_overscan) env var (in parallel - libretro.cpp
 * writes the global, this file writes vulkan_crop_overscan), so
 * their values track identically; renaming here just makes the
 * shadow explicit and stops the static-after-extern conflict
 * that fc4d742's switch to the central globals header surfaced.
 */
static int vulkan_crop_overscan;
static int image_offset_cycles;
static unsigned image_crop;
static int initial_scanline;
static int last_scanline;
static int initial_scanline_pal;
static int last_scanline_pal;
static bool frame_duping_enabled = false;
static uint32_t prev_frame_width = 320;
static uint32_t prev_frame_height = 240;
static bool show_vram = false;

static retro_video_refresh_t video_refresh_cb;

static const VkApplicationInfo *get_application_info(void)
{
   static const VkApplicationInfo info = {
      VK_STRUCTURE_TYPE_APPLICATION_INFO,
      NULL,
      "Beetle PSX",
      0,
      "parallel-psx",
      0,
      VK_MAKE_VERSION(1, 0, 32),
   };
   return &info;
}

/*
 * Dispatcher for the deferred-op queue. Called once per queued op
 * during vk_context_reset's drain. Each case re-enters the matching
 * rhi_vulkan_<op>() entry point with the captured arguments; by drain
 * time the renderer is up so the entry point's "renderer present"
 * branch executes and actually performs the work that the original
 * call could not. The user pointer is unused - everything the entry
 * points need is in this TU's file-statics.
 */
static void vk_defer_dispatch(void *user, const rhi_defer_op_t *op)
{
   (void)user;
   if (!op)
      return;

   switch (op->kind)
   {
      case RHI_DEFER_SET_TEX_WINDOW:
         rhi_vulkan_set_tex_window(op->u.set_tex_window.tww,
                                   op->u.set_tex_window.twh,
                                   op->u.set_tex_window.twx,
                                   op->u.set_tex_window.twy);
         break;
      case RHI_DEFER_SET_DRAW_OFFSET:
         rhi_vulkan_set_draw_offset(op->u.set_draw_offset.x,
                                    op->u.set_draw_offset.y);
         break;
      case RHI_DEFER_SET_DRAW_AREA:
         rhi_vulkan_set_draw_area(op->u.set_draw_area.x0,
                                  op->u.set_draw_area.y0,
                                  op->u.set_draw_area.x1,
                                  op->u.set_draw_area.y1);
         break;
      case RHI_DEFER_SET_VRAM_FRAMEBUFFER_COORDS:
         rhi_vulkan_set_vram_framebuffer_coords(
               op->u.set_vram_framebuffer_coords.xstart,
               op->u.set_vram_framebuffer_coords.ystart);
         break;
      case RHI_DEFER_SET_HORIZONTAL_DISPLAY_RANGE:
         rhi_vulkan_set_horizontal_display_range(
               op->u.set_horizontal_display_range.x1,
               op->u.set_horizontal_display_range.x2);
         break;
      case RHI_DEFER_SET_VERTICAL_DISPLAY_RANGE:
         rhi_vulkan_set_vertical_display_range(
               op->u.set_vertical_display_range.y1,
               op->u.set_vertical_display_range.y2);
         break;
      case RHI_DEFER_SET_DISPLAY_MODE:
         rhi_vulkan_set_display_mode(op->u.set_display_mode.depth_24bpp,
                                     op->u.set_display_mode.is_pal,
                                     op->u.set_display_mode.is_480i,
                                     op->u.set_display_mode.width_mode);
         break;
      case RHI_DEFER_LOAD_IMAGE:
         rhi_vulkan_load_image(op->u.load_image.x,
                               op->u.load_image.y,
                               op->u.load_image.w,
                               op->u.load_image.h,
                               op->u.load_image.vram,
                               op->u.load_image.mask_test,
                               op->u.load_image.set_mask);
         break;
      case RHI_DEFER_TOGGLE_DISPLAY:
         rhi_vulkan_toggle_display(op->u.toggle_display.status);
         break;
   }
}

static void vk_context_reset(void)
{
   if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void**)&vulkan) || !vulkan)
      return;

   if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION)
   {
      vulkan = NULL;
      return;
   }

   assert(context);
   device = (Device *)malloc(sizeof(Device));
   Device::device_init(device);
   device->set_context(*context);

   renderer = (Renderer *)malloc(sizeof(Renderer));
   new (renderer) Renderer(*device, scaling, msaa, save_state.vram.empty() ? NULL : &save_state);
   if (!renderer->is_valid())
   {
      renderer->~Renderer();
      free(renderer);
      Device::device_deinit(device);
      free(device);
      renderer = NULL;
      device = NULL;
      vulkan = NULL;
      return;
   }

   tt_log_startup("vk renderer init: scaling=%u msaa=%u has_software_fb=%d\n",
         (unsigned)scaling, (unsigned)msaa, (int)has_software_fb);

   /* Replay any rhi_vulkan_* state-sets / VRAM uploads that arrived
    * between rhi_vulkan_open's SET_HW_RENDER and this context_reset
    * firing. By this point `renderer` is non-null so each call lands
    * on the live renderer instead of being silently dropped. */
   if (rhi_defer_count(&defer) > 0)
      rhi_defer_drain(&defer, vk_defer_dispatch, NULL);

   renderer->flush();
}

static void vk_context_destroy(void)
{
   if (device == NULL)
      return;

   save_state = renderer->save_vram_state();
   vulkan     = NULL;
   scanout_handles.clear();
   swapchain_images.clear();

   renderer->~Renderer();
   free(renderer);
   Device::device_deinit(device);
   free(device);
   context_deinit(context);
   free(context);
   renderer = NULL;
   device = NULL;
   context = NULL;

   /* Free the deferred-op storage. The pre-migration C++ defer queue
    * never cleared on destroy, which meant any ops queued in the
    * (rare) gap between destroy and the next reset accumulated; that
    * was a latent leak that nobody noticed because the gap is normally
    * empty. The new policy clears here, matching gl_context_destroy
    * and keeping behaviour symmetric across backends. */
   rhi_defer_clear(&defer);
}

static bool libretro_create_device(
      struct retro_vulkan_context *libretro_context,
      VkInstance instance,
      VkPhysicalDevice gpu,
      VkSurfaceKHR surface,
      PFN_vkGetInstanceProcAddr get_instance_proc_addr,
      const char **required_device_extensions,
      unsigned num_required_device_extensions,
      const char **required_device_layers,
      unsigned num_required_device_layers,
      const VkPhysicalDeviceFeatures *required_features)
{
   if (!context_init_loader(get_instance_proc_addr))
      return false;

   if (context)
   {
      context_deinit(context);
      free(context);
      context = NULL;
   }

   /* parallel-psx's Context constructor used to throw on
    * failure; it now sets a valid=false flag instead. The
    * try/catch boundary is gone with it. */
   context = (Context *)malloc(sizeof(Context));
   if (!context)
      return false;
   context_init(context, instance, gpu, surface, required_device_extensions, num_required_device_extensions,
                                 required_device_layers, num_required_device_layers,
                                 required_features);
   if (!context_is_valid(context))
   {
      context_deinit(context);
      free(context);
      context = NULL;
      return false;
   }

   context_release_device(context);
   libretro_context->gpu = context_get_gpu(context);
   libretro_context->device = context_get_device(context);
   libretro_context->presentation_queue = context_get_graphics_queue(context);
   libretro_context->presentation_queue_family_index = context_get_graphics_queue_family(context);
   libretro_context->queue = context_get_graphics_queue(context);
   libretro_context->queue_family_index = context_get_graphics_queue_family(context);
   return true;
}

bool rhi_vulkan_open(bool is_pal)
{
   libretro_log   = log_cb;
   content_is_pal = is_pal;

   hw_render.context_type    = RETRO_HW_CONTEXT_VULKAN;
   hw_render.version_major   = VK_MAKE_VERSION(1, 0, 32);
   hw_render.version_minor   = 0;
   hw_render.context_reset   = vk_context_reset;
   hw_render.context_destroy = vk_context_destroy;
   hw_render.cache_context   = false;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
      RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,

      get_application_info,
      libretro_create_device,
      NULL,
   };

   environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void*)&iface);

   return true;
}

void rhi_vulkan_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void rhi_vulkan_set_video_refresh(retro_video_refresh_t cb)
{
   video_refresh_cb = cb;
}

void rhi_vulkan_get_system_av_info(struct retro_system_av_info *info)
{
   rhi_vulkan_refresh_variables();

   memset(info, 0, sizeof(*info));

   // Set retro_game_geometry
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W * (super_sampling ? 1 : scaling);
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H * (super_sampling ? 1 : scaling);
   info->geometry.aspect_ratio = rhi_common_get_aspect_ratio(content_is_pal, vulkan_crop_overscan,
                                       content_is_pal ? initial_scanline_pal : initial_scanline,
                                       content_is_pal ? last_scanline_pal : last_scanline,
                                       aspect_ratio_setting, show_vram, widescreen_hack, widescreen_hack_aspect_ratio_setting);

   // Set retro_system_timing
   info->timing.fps = rhi_common_get_timing_fps();
   info->timing.sample_rate = SOUND_FREQUENCY;
}

void rhi_vulkan_refresh_variables(void)
{
   struct retro_variable var = {0};

   var.key = BEETLE_OPT(renderer_software_fb);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         has_software_fb = true;
      else
         has_software_fb = false;
   }
   else
      /* If 'BEETLE_OPT(renderer_software_fb)' option is not found, then
       * we are running in software mode */
      has_software_fb = true;

   tt_log_startup("rhi_vulkan_refresh_variables: has_software_fb=%d\n",
         (int)has_software_fb);

   unsigned old_scaling = scaling;
   unsigned old_msaa = msaa;
   bool old_super_sampling = super_sampling;
   bool old_show_vram = show_vram;
   int old_crop_overscan = vulkan_crop_overscan;
   unsigned old_image_crop = image_crop;
   bool old_widescreen_hack = widescreen_hack;
   unsigned old_widescreen_hack_aspect_ratio_setting = widescreen_hack_aspect_ratio_setting;
   bool visible_scanlines_changed = false;

   var.key = BEETLE_OPT(internal_resolution);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      /* Same limitations as libretro.cpp */
      scaling = var.value[0] - '0';
      if (var.value[1] != 'x')
      {
         scaling  = (var.value[0] - '0') * 10;
         scaling += var.value[1] - '0';
      }
   }

   var.key = BEETLE_OPT(scaled_uv_offset);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         scaled_uv_offset = true;
      else
         scaled_uv_offset = false;
   }

   var.key = BEETLE_OPT(filter_exclude_sprite);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "all"))
         filter_exclude_sprites = 2;
      else if (!strcmp(var.value, "opaque"))
         filter_exclude_sprites = 1;
      else
         filter_exclude_sprites = 0;
   }

   var.key = BEETLE_OPT(filter_exclude_2d_polygon);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "all"))
         filter_exclude_2d_polygons = 2;
      else if (!strcmp(var.value, "opaque"))
         filter_exclude_2d_polygons = 1;
      else
         filter_exclude_2d_polygons = 0;
   }

   var.key = BEETLE_OPT(adaptive_smoothing);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         adaptive_smoothing = true;
      else
         adaptive_smoothing = false;
   }

   var.key = BEETLE_OPT(super_sampling);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         super_sampling = true;
      else
         super_sampling = false;
   }

   var.key = BEETLE_OPT(msaa);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      msaa = strtoul(var.value, NULL, 0);
   }

   var.key = BEETLE_OPT(mdec_yuv);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         mdec_yuv = true;
      else
         mdec_yuv = false;
   }

   var.key = BEETLE_OPT(dither_mode);
   dither_mode = DITHER_NATIVE;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "internal resolution"))
         dither_mode = DITHER_UPSCALED;
      else if (!strcmp(var.value, "disabled"))
         dither_mode = DITHER_OFF;
   }

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         vulkan_crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         vulkan_crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         vulkan_crop_overscan = 2;
   }

   var.key = BEETLE_OPT(image_offset_cycles);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      image_offset_cycles = atoi(var.value);
   }
   
   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_initial_scanline = atoi(var.value);
      if (new_initial_scanline != initial_scanline)
      {
         initial_scanline = new_initial_scanline;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_last_scanline = atoi(var.value);
      if (new_last_scanline != last_scanline)
      {
         last_scanline = new_last_scanline;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_initial_scanline_pal = atoi(var.value);
      if (new_initial_scanline_pal != initial_scanline_pal)
      {
         initial_scanline_pal = new_initial_scanline_pal;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_last_scanline_pal = atoi(var.value);
      if (new_last_scanline_pal != last_scanline_pal)
      {
         last_scanline_pal = new_last_scanline_pal;
         visible_scanlines_changed = true;
      }
   }

   var.key = BEETLE_OPT(widescreen_hack);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         widescreen_hack = true;
      else
         widescreen_hack = false;
   }

   var.key = BEETLE_OPT(widescreen_hack_aspect_ratio);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "16:10"))
         widescreen_hack_aspect_ratio_setting = 0;
      else if (!strcmp(var.value, "16:9"))
         widescreen_hack_aspect_ratio_setting = 1;
      else if (!strcmp(var.value, "18:9"))
         widescreen_hack_aspect_ratio_setting = 2;
      else if (!strcmp(var.value, "19:9"))
         widescreen_hack_aspect_ratio_setting = 3;
      else if (!strcmp(var.value, "20:9"))
         widescreen_hack_aspect_ratio_setting = 4;
      else if (!strcmp(var.value, "21:9"))
         widescreen_hack_aspect_ratio_setting = 5;
      else if (!strcmp(var.value, "32:9"))
         widescreen_hack_aspect_ratio_setting = 6;
   }

   var.key = BEETLE_OPT(track_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         track_textures = true;
      else
         track_textures = false;
   }

   var.key = BEETLE_OPT(dump_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         dump_textures = true;
      else
         dump_textures = false;
   }

   var.key = BEETLE_OPT(replace_textures);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         replace_textures = true;
      else
         replace_textures = false;
   }

   var.key = BEETLE_OPT(hd_cache_vram_budget);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      hd_cache_vram_bytes = (size_t)atoi(var.value) * 1024 * 1024;

   var.key = BEETLE_OPT(hd_cache_ram_budget);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      hd_cache_ram_bytes = (size_t)atoi(var.value) * 1024 * 1024;

   var.key = BEETLE_OPT(hd_caching_method);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      eager_hd_textures = (strcmp(var.value, "lazy") != 0); // "eager" (default) or "lazy"

   struct retro_core_option_display option_display;
   option_display.visible = track_textures;

   option_display.key = BEETLE_OPT(dump_textures);
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   option_display.key = BEETLE_OPT(replace_textures);
   environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

   var.key = BEETLE_OPT(frame_duping);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
      {
         bool frontend_can_dupe = false;
         if (environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &frontend_can_dupe))
         {
            frame_duping_enabled = frontend_can_dupe;
            if (!frontend_can_dupe)
               log_cb(RETRO_LOG_INFO, "Frontend does not support frame duping. Frame duping will be disabled.\n");
         }
      }
      else
         frame_duping_enabled = false;
   }

   var.key = BEETLE_OPT(display_vram);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "enabled"))
         show_vram = true;
      else
         show_vram = false;
   }

   // Clean this up. Possible to categorize by order of severity, e.g. geometry dirty flag vs full system_av dirty flag
   if ((old_scaling != scaling ||
        old_super_sampling != super_sampling ||
        old_msaa != msaa ||
        old_show_vram != show_vram ||
        old_crop_overscan != vulkan_crop_overscan ||
        old_image_crop != image_crop ||
        old_widescreen_hack != widescreen_hack ||
        old_widescreen_hack_aspect_ratio_setting != widescreen_hack_aspect_ratio_setting ||
        visible_scanlines_changed)
       && renderer)
   {
      // Potential bad behavior from calling rhi_vulkan_get_system_av_info() from inside
      // rhi_vulkan_refresh_variables() since both functions call each other...
      retro_system_av_info info;
      rhi_vulkan_get_system_av_info(&info);

      if (!environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info))
      {
         // Failed to change scale, just keep using the old one.
         scaling = old_scaling;
      }
   }
}

static void ensure_sync_index_resources(void)
{
   unsigned mask = vulkan->get_sync_index_mask(vulkan->handle);
   unsigned num_frames = 0;
   for (unsigned i = 0; i < 32; i++)
      if (mask & (1u << i))
         num_frames = i + 1;

   if (num_frames != swapchain_images.size())
   {
      swapchain_images.resize(num_frames);
      scanout_handles.resize(num_frames);
   }
}

void rhi_vulkan_prepare_frame(void)
{
   if (device == NULL)
   {
      rhi_type = RHI_SOFTWARE;
      return;
   }

   inside_frame = true;
   device->flush_frame();
   vulkan->wait_sync_index(vulkan->handle);
   ensure_sync_index_resources();
   unsigned index = vulkan->get_sync_index(vulkan->handle);
   device->next_frame_context();

   renderer->set_scaled_uv_offset(scaled_uv_offset);
   renderer->set_filter_mode((Renderer::FilterMode)(filter_mode));
   renderer->set_sprite_filter_exclude((Renderer::FilterExclude)(filter_exclude_sprites));
   renderer->set_polygon_2d_filter_exclude((Renderer::FilterExclude)(filter_exclude_2d_polygons));
}

static Renderer::ScanoutMode get_scanout_mode(bool bpp24)
{
   if (bpp24)
      return Renderer::ScanoutMode_BGR24;
   else if (dither_mode != DITHER_OFF)
      return Renderer::ScanoutMode_ABGR1555_Dither;
   else
      return Renderer::ScanoutMode_ABGR1555_555;
}

void rhi_vulkan_finalize_frame(const void *fb, unsigned width,
                               unsigned height, unsigned pitch)
{
   if (device == NULL)
      return;

   tt_log("vk finalize_frame display=%ux%u\n",
         (unsigned)width, (unsigned)height);
   tt_frame_advance();

   if (frame_duping_enabled && !GPU_get_display_change_count())
   {
      /* Any visual core option changes will be deferred to next non-duped frame */

      //printf("No PSX GPU display update; duping frame\n");
      renderer->flush();
      video_refresh_cb(NULL, prev_frame_width, prev_frame_height, 0);

      inside_frame = false;
      return;
   }

   renderer->set_track_textures(track_textures);
   renderer->set_dump_textures(dump_textures);
   renderer->set_replace_textures(replace_textures);
   renderer->set_hd_cache_budgets(hd_cache_ram_bytes, hd_cache_vram_bytes);
   renderer->set_eager_hd_textures(eager_hd_textures);
   renderer->set_adaptive_smoothing(adaptive_smoothing);
   renderer->set_dither_native_resolution(dither_mode == DITHER_NATIVE);
   renderer->set_horizontal_overscan_cropping(vulkan_crop_overscan);
   renderer->set_horizontal_offset_cycles(image_offset_cycles);
   renderer->set_visible_scanlines(initial_scanline, last_scanline, initial_scanline_pal, last_scanline_pal);
   renderer->set_horizontal_additional_cropping(image_crop);

   renderer->set_display_filter(super_sampling ? Renderer::ScanoutFilter_SSAA : Renderer::ScanoutFilter_None);
   if (renderer->get_scanout_mode() == Renderer::ScanoutMode_BGR24)
      renderer->set_mdec_filter(mdec_yuv ? Renderer::ScanoutFilter_MDEC_YUV : Renderer::ScanoutFilter_None);
   else
      renderer->set_mdec_filter(Renderer::ScanoutFilter_None);

   auto scanout = show_vram ? renderer->scanout_vram_to_texture() : renderer->scanout_to_texture();
   unsigned index = vulkan->get_sync_index(vulkan->handle);

   retro_vulkan_image *image                          = &swapchain_images[index];

   image->create_info.sType                           = 
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   image->create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
   image->create_info.format                          = image_get_format(ih_get(&scanout));
   image->create_info.subresourceRange.baseMipLevel   = 0;
   image->create_info.subresourceRange.baseArrayLayer = 0;
   image->create_info.subresourceRange.levelCount     = 1;
   image->create_info.subresourceRange.layerCount     = 1;
   image->create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
   image->create_info.components.r                    = VK_COMPONENT_SWIZZLE_R;
   image->create_info.components.g                    = VK_COMPONENT_SWIZZLE_G;
   image->create_info.components.b                    = VK_COMPONENT_SWIZZLE_B;
   image->create_info.components.a                    = VK_COMPONENT_SWIZZLE_A;
   image->create_info.image                           = image_get_image(ih_get(&scanout));
   image->image_layout                                = 
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   image->image_view                                  = 
      imageview_get_view(&image_get_view(ih_get(&scanout)));

   vulkan->set_image(vulkan->handle, image, 0,
         NULL, VK_QUEUE_FAMILY_IGNORED);
   renderer->flush();

   scanout_handles[index] = scanout;
   video_refresh_cb(RETRO_HW_FRAME_BUFFER_VALID, image_get_width(ih_get(&scanout), 0), image_get_height(ih_get(&scanout), 0), 0);
   inside_frame = false;

   prev_frame_width = image_get_width(ih_get(&scanout), 0);
   prev_frame_height = image_get_height(ih_get(&scanout), 0);
}

/* Draw commands */

void rhi_vulkan_set_tex_window(uint8_t tww, uint8_t twh,
                               uint8_t twx, uint8_t twy)
{
   uint8_t tex_x_mask = ~(tww << 3);
   uint8_t tex_y_mask = ~(twh << 3);
   uint8_t tex_x_or   = (twx & tww) << 3;
   uint8_t tex_y_or   = (twy & twh) << 3;

   if (renderer)
   {
      renderer->set_texture_window({ tex_x_mask, tex_y_mask, tex_x_or, tex_y_or });
      tt_log("vk set_tex_window tww=%u twh=%u twx=%u twy=%u\n",
            (unsigned)tww, (unsigned)twh, (unsigned)twx, (unsigned)twy);
   }
   else
      rhi_defer_push_set_tex_window(&defer, tww, twh, twx, twy);
}

void rhi_vulkan_set_draw_offset(int16_t x, int16_t y)
{
   if (renderer)
      renderer->set_draw_offset(x, y);
   else
      rhi_defer_push_set_draw_offset(&defer, x, y);
}

void rhi_vulkan_set_draw_area(uint16_t x0, uint16_t y0,
                              uint16_t x1, uint16_t y1)
{
   int width  = x1 - x0 + 1;
   int height = y1 - y0 + 1;
   if (width  < 0) width  = 0;
   if (height < 0) height = 0;

   {
      int w_max = int(FB_WIDTH  - x0);
      int h_max = int(FB_HEIGHT - y0);
      if (width  > w_max) width  = w_max;
      if (height > h_max) height = h_max;
   }

   if (renderer)
   {
      renderer->set_draw_rect({ x0, y0, unsigned(width), unsigned(height) });
      tt_log("vk set_draw_area top_left=(%u,%u) bot_right_inclusive=(%u,%u)\n",
            (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1);
   }
   else
      /* Defer the raw inputs (x0,y0,x1,y1); the dispatcher re-enters
       * this entry point which redoes the width/height clamp on top of
       * a live FB_WIDTH/FB_HEIGHT. Functionally identical to capturing
       * the post-clamp values, just keeps the clamp in one place. */
      rhi_defer_push_set_draw_area(&defer, x0, y0, x1, y1);
}

void rhi_vulkan_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
   if (renderer)
      renderer->set_vram_framebuffer_coords(xstart, ystart);
   else
      rhi_defer_push_set_vram_framebuffer_coords(&defer, xstart, ystart);
}

void rhi_vulkan_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
   if (renderer)
      renderer->set_horizontal_display_range(x1, x2);
   else
      rhi_defer_push_set_horizontal_display_range(&defer, x1, x2);
}

void rhi_vulkan_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
   if (renderer)
      renderer->set_vertical_display_range(y1, y2);
   else
      rhi_defer_push_set_vertical_display_range(&defer, y1, y2);
}

void rhi_vulkan_set_display_mode(bool depth_24bpp,
                                 bool is_pal,
                                 bool is_480i,
                                 int width_mode)
{
   if (renderer)
      renderer->set_display_mode(get_scanout_mode(depth_24bpp), is_pal,
                                 is_480i, (Renderer::WidthMode)(width_mode));
   else
      rhi_defer_push_set_display_mode(&defer, depth_24bpp, is_pal,
                                      is_480i, width_mode);
}

void rhi_vulkan_push_triangle(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode_ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode_Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode_Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode_None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode_None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode_Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode_Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode_Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode_AddQuarter);
         break;
   }

   renderer->set_primitive_type(PrimitiveType_Polygon);

   Vertex vertices[3] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
   };

   renderer->draw_triangle(vertices);
}

void rhi_vulkan_push_quad(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      float p3x, float p3y, float p3w,
      uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
      uint16_t t0x, uint16_t t0y, 
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t t3x, uint16_t t3y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask,
      bool is_sprite, bool may_be_2d)
{
   if (!renderer)
      return;

   renderer->set_texture_color_modulate(texture_blend_mode == 2);
   renderer->set_palette_offset(clut_x, clut_y);
   renderer->set_texture_offset(texpage_x, texpage_y);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->set_UV_limits(min_u, min_v, max_u, max_v);
   if (texture_blend_mode != 0)
   {
      switch (depth_shift)
      {
         default:
         case 0:
            renderer->set_texture_mode(TextureMode_ABGR1555);
            break;
         case 1:
            renderer->set_texture_mode(TextureMode_Palette8bpp);
            break;
         case 2:
            renderer->set_texture_mode(TextureMode_Palette4bpp);
            break;
      }
   }
   else
      renderer->set_texture_mode(TextureMode_None);

   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode_None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode_Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode_Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode_Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode_AddQuarter);
         break;
   }

   if (is_sprite)
      renderer->set_primitive_type(PrimitiveType_Sprite);
   else if (may_be_2d)
      renderer->set_primitive_type(PrimitiveType_May_Be_2D_Polygon);
   else
      renderer->set_primitive_type(PrimitiveType_Polygon);

   Vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };

   renderer->draw_quad(vertices);
}

void rhi_vulkan_push_line(
      int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   renderer->set_texture_mode(TextureMode_None);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   switch (blend_mode)
   {
      default:
         renderer->set_semi_transparent(SemiTransparentMode_None);
         break;

      case 0:
         renderer->set_semi_transparent(SemiTransparentMode_Average);
         break;
      case 1:
         renderer->set_semi_transparent(SemiTransparentMode_Add);
         break;
      case 2:
         renderer->set_semi_transparent(SemiTransparentMode_Sub);
         break;
      case 3:
         renderer->set_semi_transparent(SemiTransparentMode_AddQuarter);
         break;
   }

   Vertex vertices[2] = {
      { float(p0x), float(p0y), 1.0f, c0, 0, 0 },
      { float(p1x), float(p1y), 1.0f, c1, 0, 0 },
   };
   renderer->set_texture_color_modulate(false);
   renderer->draw_line(vertices);
}

void rhi_vulkan_load_image(
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram,
      bool mask_test, bool set_mask)
{
   if (!renderer)
   {
      /* Pre-context_reset uploads (e.g. savestate-load arriving before
       * the Vulkan context is up, or game boot pushing VRAM ahead of
       * the frontend's context_reset). The captured `vram` pointer
       * aliases GPU.vram, which lives for the whole core lifetime, so
       * holding it across the deferred-replay window is safe. */
      rhi_defer_push_load_image(&defer, x, y, w, h, vram, mask_test, set_mask);
      return;
   }

   tt_log("vk load_image rect=(%u,%u %ux%u) mask_test=%d set_mask=%d\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);

   renderer->notify_texture_upload(Rect { x, y, w, h }, vram);
   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   auto handle   = renderer->copy_cpu_to_vram({ x, y, w, h });
   uint16_t *tmp = renderer->begin_copy(handle);

   /* The row loop has two independent invariants: x-wrap (dual_copy)
    * and y-wrap. Both are determined entirely by (x, y, w, h) before
    * the loop runs. Hoisting them out lets the inner loops degenerate
    * to a single straight memcpy in the common no-wrap case and lets
    * the compiler/CPU prefetcher see the full source-pointer stride.
    *
    * The (y + off_y) & (FB_HEIGHT - 1) mask in the original code was
    * a no-op for every iteration when y + h <= FB_HEIGHT (the dominant
    * case for upload rects), but the compiler can't prove that without
    * splitting the loop.
    */
   const bool dual_copy = x + w > FB_WIDTH;
   const bool y_wrap    = y + h > FB_HEIGHT;

   if (dual_copy)
   {
      const unsigned first  = FB_WIDTH - x;
      const unsigned second = w - first;
      if (y_wrap)
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
         {
            const uint16_t *row = vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH;
            memcpy(tmp + off_y * w,         row + x, first  * sizeof(uint16_t));
            memcpy(tmp + off_y * w + first, row,     second * sizeof(uint16_t));
         }
      }
      else
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
         {
            const uint16_t *row = vram + (y + off_y) * FB_WIDTH;
            memcpy(tmp + off_y * w,         row + x, first  * sizeof(uint16_t));
            memcpy(tmp + off_y * w + first, row,     second * sizeof(uint16_t));
         }
      }
   }
   else
   {
      if (y_wrap)
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
            memcpy(tmp + off_y * w,
                  vram + ((y + off_y) & (FB_HEIGHT - 1)) * FB_WIDTH + x,
                  w * sizeof(uint16_t));
      }
      else
      {
         for (unsigned off_y = 0; off_y < h; off_y++)
            memcpy(tmp + off_y * w,
                  vram + (y + off_y) * FB_WIDTH + x,
                  w * sizeof(uint16_t));
      }
   }
   renderer->end_copy(handle);

   // This is called on state loading. 
   if (!inside_frame)
      renderer->flush();
}

bool rhi_vulkan_read_vram(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h,
                          uint16_t *vram)
{
   if (!renderer)
      return false;

   tt_log("vk read_vram rect=(%u,%u %ux%u)\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);

   renderer->copy_vram_to_cpu_synchronous({ x, y, w, h }, vram);
   return true;
}

void rhi_vulkan_fill_rect(uint32_t color,
                          uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h)
{
   if (renderer)
   {
      tt_log("vk fill_rect rect=(%u,%u %ux%u) color=0x%06x\n",
            (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
            (unsigned)(color & 0xFFFFFFu));
      renderer->clear_rect({ x, y, w, h }, color);
   }
}

void rhi_vulkan_copy_rect(uint16_t src_x, uint16_t src_y,
                          uint16_t dst_x, uint16_t dst_y,
                          uint16_t w, uint16_t h, 
                          bool mask_test, bool set_mask)
{
   if (!renderer)
      return;

   tt_log("vk copy_rect src=(%u,%u) dst=(%u,%u) %ux%u mask_test=%d set_mask=%d\n",
         (unsigned)src_x, (unsigned)src_y,
         (unsigned)dst_x, (unsigned)dst_y,
         (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);

   renderer->set_mask_test(mask_test);
   renderer->set_force_mask_bit(set_mask);
   renderer->blit_vram({ dst_x, dst_y, w, h }, { src_x, src_y, w, h });
}

void rhi_vulkan_toggle_display(bool status)
{
   if (renderer)
      renderer->toggle_display(status == 0);
   else
      rhi_defer_push_toggle_display(&defer, status);
}

bool rhi_vulkan_has_software_renderer(void)
{
   return has_software_fb;
}
