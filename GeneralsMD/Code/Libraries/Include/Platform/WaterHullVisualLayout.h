// Bounded render-only hull mesh transport. Engine registers and native shader slots are
// different layouts: the pixel common block extends the unchanged vertex common prefix.
#pragma once
namespace WaterHullVisualLayout {
enum : unsigned {
    MaxHulls=8, MaxVertices=64, MaxTriangles=96, RegistersPerHull=88,
    PacketRegisters=MaxHulls*RegistersPerHull,
    MetadataRegister=176, PacketRegister=192, PixelRegisterCount=896,
    FragmentCommonRegisters=194, VertexCommonRegisters=186,
    BlockCount=3, Block0Registers=256, Block1Registers=256, Block2Registers=192
};
constexpr unsigned BlockRegisters(unsigned block) {
    if(block==0) return Block0Registers;
    if(block==1) return Block1Registers;
    if(block==2) return Block2Registers;
    return 0;
}
constexpr unsigned BlockOffset(unsigned block) {
    if(block==0) return 0;
    if(block==1) return Block0Registers;
    if(block==2) return Block0Registers+Block1Registers;
    return PacketRegisters;
}
static_assert(RegistersPerHull==MaxVertices+MaxTriangles/4,"four packed triangle indices per float4");
static_assert(PacketRegister+PacketRegisters==PixelRegisterCount,"bounded emulated bank");
static_assert(Block0Registers+Block1Registers+Block2Registers==PacketRegisters,"complete packet partition");
static_assert(VertexCommonRegisters+MaxHulls==FragmentCommonRegisters,"metadata follows original vertex prefix");
}
