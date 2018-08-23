// Copyright (C) 2009-2018, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/renderer/Common.h>
#include <shaders/glsl_cpp_common/ClusteredShading.h>

namespace anki
{

// Forward
class ThreadHiveSemaphore;
class Config;

/// @addtogroup renderer
/// @{

/// @memberof ClusterBin
class ClusterBinIn
{
public:
	ThreadHive* m_threadHive ANKI_DBG_NULLIFY;

	const RenderQueue* m_renderQueue ANKI_DBG_NULLIFY;

	StagingGpuMemoryManager* m_stagingMem ANKI_DBG_NULLIFY;

	Bool m_shadowsEnabled ANKI_DBG_NULLIFY;
};

/// @memberof ClusterBin
class ClusterBinOut
{
public:
	StagingGpuMemoryToken m_pointLightsToken;
	StagingGpuMemoryToken m_spotLightsToken;
	StagingGpuMemoryToken m_probesToken;
	StagingGpuMemoryToken m_decalsToken;
	StagingGpuMemoryToken m_clustersToken;
	StagingGpuMemoryToken m_indicesToken;

	TextureViewPtr m_diffDecalTexView;
	TextureViewPtr m_specularRoughnessDecalTexView;

	ClustererMagicValues m_shaderMagicValues;
};

/// Bins lights, probes, decals etc to clusters.
class ClusterBin
{
public:
	void init(U32 clusterCountX, U32 clusterCountY, U32 clusterCountZ, const ConfigSet& cfg);

	void bin(ClusterBinIn& in, ClusterBinOut& out);

private:
	class BinCtx;

	Array<U32, 3> m_clusterCounts = {};
	U32 m_totalClusterCount = 0;
	U32 m_indexCount = 0;

	void prepare(BinCtx& ctx);

	Bool processNextCluster(BinCtx& ctx) const;

	void writeTypedObjectsToGpuBuffers(BinCtx& ctx) const;

	static void writeTypedObjectsToGpuBuffersCallback(
		void* userData, U32 threadId, ThreadHive& hive, ThreadHiveSemaphore* signalSemaphore);

	static void binToClustersCallback(
		void* userData, U32 threadId, ThreadHive& hive, ThreadHiveSemaphore* signalSemaphore);
};
/// @}

} // end namespace anki