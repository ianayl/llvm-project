//===-- IntelGPUTargetParser - Parser for Intel GPU targets ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser for the Intel GPU list.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/IntelGPUTargetParser.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include <cassert>

using namespace llvm;
using namespace IntelGPU;

// A GPU IP version packs four fields, from the most significant bit down:
//
//    31           22 21      14 13       6 5        0
//   +---------------+----------+----------+----------+
//   |     major     |   minor  | reserved | revision |
//   +---------------+----------+----------+----------+
//        10 bits      8 bits     8 bits     6 bits
//
// The reserved bits carry no information.
static constexpr uint32_t GPUIPMajorShift = 22;
static constexpr uint32_t GPUIPMinorShift = 14;
[[maybe_unused]] static constexpr uint32_t GPUIPMajorMask = 0x3ff;
static constexpr uint32_t GPUIPMinorMask = 0xff;
static constexpr uint32_t GPUIPRevisionMask = 0x3f;

// The bits that identify a device: the major and the minor version. Neither the
// revision nor the reserved bits take part in the lookup, because every
// revision of a device is one device as far as the compiler is concerned.
static constexpr uint32_t GPUIPDeviceMask = ~0u << GPUIPMinorShift;

// Pack a major and a minor version the way a GPU IP version does, so that a
// row of the table can be compared against a reported version as it is. A value
// too wide for its field would silently corrupt the fields above it, which
// would mean a typo in IntelGPUTargetParser.def going unnoticed.
static constexpr uint32_t packDevice(uint32_t Major, uint32_t Minor) {
  assert((Major & ~GPUIPMajorMask) == 0 && "major version too wide");
  assert((Minor & ~GPUIPMinorMask) == 0 && "minor version too wide");
  return (Major << GPUIPMajorShift) | (Minor << GPUIPMinorShift);
}

StringRef llvm::IntelGPU::getArchName(uint32_t GPUIPVersion) {
  const uint32_t Device = GPUIPVersion & GPUIPDeviceMask;
#define INTEL_GPU(NAME, KIND, MAJOR, MINOR, IGCA_TARGET, IGCA_FEATURE_SETS)    \
  if (Device == packDevice(MAJOR, MINOR))                                      \
    return NAME;
#include "llvm/TargetParser/IntelGPUTargetParser.def"
  return "";
}

std::string llvm::IntelGPU::getNumericArchName(uint32_t GPUIPVersion) {
  const uint32_t Major = GPUIPVersion >> GPUIPMajorShift;
  const uint32_t Minor = (GPUIPVersion >> GPUIPMinorShift) & GPUIPMinorMask;
  const uint32_t Revision = GPUIPVersion & GPUIPRevisionMask;
  return ("xe_" + Twine(Major) + "." + Twine(Minor) + "." + Twine(Revision))
      .str();
}

namespace {
struct IGCATargetEntry {
  StringLiteral Name;
  IGCATarget Target;
};
} // namespace

/// Obtain a list of valid IGCA targets.
///
/// We derive a valid list of IGCA targets from INTEL_GPU and INTEL_GPU_COMPAT
/// defined in IntelGPUTargetParser.def.
static ArrayRef<IGCATargetEntry> getValidIGCATargets() {
// "Derivation rules" for inferring core/non-exact rules:
#define IGCA_IMPLIED_Core(X, T) X(T, Core)
#define IGCA_IMPLIED_Compute(X, T) X(T, Core) X(T, Compute)
#define IGCA_IMPLIED_ComputeExact(X, T)                                        \
  X(T, Core) X(T, Compute) X(T, ComputeExact)
#define IGCA_IMPLIED_Render(X, T) X(T, Core) X(T, Render)
#define IGCA_IMPLIED_RenderExact(X, T) X(T, Core) X(T, Render) X(T, RenderExact)

#define IGCA_SUFFIX_Core ""
#define IGCA_SUFFIX_Compute "c"
#define IGCA_SUFFIX_ComputeExact "ca"
#define IGCA_SUFFIX_Render "r"
#define IGCA_SUFFIX_RenderExact "ra"

  static constexpr IGCATargetEntry ImpliedTargets[] = {
#define IGCA_ENTRY(T, FS)                                                      \
  {"igca_" #T IGCA_SUFFIX_##FS, IGCATarget(T, IGCAFeatureSet::FS)},
#define INTEL_GPU(NAME, KIND, MAJOR, MINOR, IGCA_TARGET, IGCA_FEATURE_SETS)    \
  IGCA_IMPLIED_##IGCA_FEATURE_SETS(IGCA_ENTRY, IGCA_TARGET)
#define INTEL_GPU_COMPAT(NAME, KIND, IGCA_TARGET, IGCA_FEATURE_SETS)           \
  IGCA_IMPLIED_##IGCA_FEATURE_SETS(IGCA_ENTRY, IGCA_TARGET)
#include "llvm/TargetParser/IntelGPUTargetParser.def"
#undef IGCA_ENTRY
  };

#undef IGCA_IMPLIED_Core
#undef IGCA_IMPLIED_Compute
#undef IGCA_IMPLIED_ComputeExact
#undef IGCA_IMPLIED_Render
#undef IGCA_IMPLIED_RenderExact

#undef IGCA_SUFFIX_Core
#undef IGCA_SUFFIX_Compute
#undef IGCA_SUFFIX_ComputeExact
#undef IGCA_SUFFIX_Render
#undef IGCA_SUFFIX_RenderExact

  // Remove all duplicate entries produced above.
  static const SmallVector<IGCATargetEntry, 0> Targets = [] {
    llvm::ArrayRef<const IGCATargetEntry> IT = ImpliedTargets;
    SmallVector<IGCATargetEntry, 0> V(IT.begin(), IT.end());
    llvm::sort(V, [](const IGCATargetEntry &A, const IGCATargetEntry &B) {
      return A.Target.pack() < B.Target.pack();
    });

    auto isTargetEq = [](const IGCATargetEntry &A, const IGCATargetEntry &B) {
      return A.Target == B.Target;
    };
    V.erase(llvm::unique(V, isTargetEq), V.end());

    return V;
  }();
  return Targets;
}

IGCATarget llvm::IntelGPU::parseIGCATarget(StringRef MaybeTarget) {
  for (const IGCATargetEntry &E : getValidIGCATargets())
    if (E.Name == MaybeTarget)
      return E.Target;
  return IGCATarget::invalid();
}

StringRef llvm::IntelGPU::getIGCATargetName(IGCATarget T) {
  for (const IGCATargetEntry &E : getValidIGCATargets())
    if (E.Target == T)
      return E.Name;
  return "";
}

// TODO: Ensure -fsycl --offload-arch provides a list of valid IGCA
// architectures, similar to how compiling for an nvptx triple returns a list
// of valid GPU architectures. The user trying to input an invalid IGCA target
// without being told what IGCA targets actually exist might get confusing.
void llvm::IntelGPU::fillValidIGCATargetList(
    SmallVectorImpl<StringRef> &Values) {
  for (const IGCATargetEntry &E : getValidIGCATargets())
    Values.push_back(E.Name);
}
