#include "Shader.h"

#include <format>
#include <stdexcept>

_NPGS_BEGIN
_RUNTIME_BEGIN
_ASSET_BEGIN

template <typename DescriptorInfoType>
requires std::is_class_v<DescriptorInfoType>
inline void FShader::WriteSharedDescriptors(std::uint32_t Set, std::uint32_t Binding, vk::DescriptorType Type,
                                            const std::vector<DescriptorInfoType>& DescriptorInfos)
{
    auto SetIt = _DescriptorSetsMap.find(Set);
    if (SetIt == _DescriptorSetsMap.end())
    {
        return;
    }

    for (const auto& DescriptorSet : SetIt->second)
    {
        if (!DescriptorSet)
        {
            throw std::runtime_error(std::format("Cannot write null descriptor set {}, binding {}", Set, Binding));
        }
        DescriptorSet.Write(DescriptorInfos, Type, Binding);
    }

    MarkAllFramesForUpdate();
}

template <typename DescriptorInfoType>
requires std::is_class_v<DescriptorInfoType>
inline void FShader::WriteDynamicDescriptors(std::uint32_t Set, std::uint32_t Binding, std::uint32_t FrameIndex,
                                             vk::DescriptorType Type, const std::vector<DescriptorInfoType>& DescriptorInfos)
{
    auto SetIt = _DescriptorSetsMap.find(Set);
    if (SetIt == _DescriptorSetsMap.end())
    {
        return;
    }

    if (FrameIndex >= SetIt->second.size())
    {
        throw std::out_of_range(std::format("Invalid descriptor frame {} for set {}, binding {}",
                                           FrameIndex, Set, Binding));
    }
    const auto& DescriptorSet = SetIt->second[FrameIndex];
    if (!DescriptorSet)
    {
        throw std::runtime_error(std::format("Cannot write null descriptor set {}, binding {}, frame {}",
                                            Set, Binding, FrameIndex));
    }
    DescriptorSet.Write(DescriptorInfos, Type, Binding);

    MarkAllFramesForUpdate();
}

NPGS_INLINE std::vector<vk::PushConstantRange> FShader::GetPushConstantRanges() const
{
    return _ReflectionInfo.PushConstants;
}

NPGS_INLINE std::uint32_t FShader::GetPushConstantOffset(const std::string& Name) const
{
    return _PushConstantOffsetsMap.at(Name);
}

NPGS_INLINE const std::vector<vk::VertexInputBindingDescription>& FShader::GetVertexInputBindings() const
{
    return _ReflectionInfo.VertexInputBindings;
}

NPGS_INLINE const std::vector<vk::VertexInputAttributeDescription>& FShader::GetVertexInputAttributes() const
{
    return _ReflectionInfo.VertexInputAttributes;
}

NPGS_INLINE const std::vector<vk::DescriptorSet>& FShader::GetDescriptorSets(std::uint32_t FrameIndex)
{
    UpdateDescriptorSets(FrameIndex);
    return _DescriptorSets[FrameIndex];
}

NPGS_INLINE void FShader::MarkAllFramesForUpdate()
{
    _DescriptorSetsUpdateMask = 0xFFFFFFFF;
}

_ASSET_END
_RUNTIME_END
_NPGS_END
