// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_WIN32_H_
#define CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_WIN32_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "chrome/installer/zucchini/address_translator.h"
#include "chrome/installer/zucchini/buffer_view.h"
#include "chrome/installer/zucchini/disassembler.h"
#include "chrome/installer/zucchini/image_utils.h"
#include "chrome/installer/zucchini/type_win_pe.h"

namespace zucchini {

class Rel32FinderX86;
class Rel32FinderX64;

struct Win32X86Traits {
  static constexpr ExecutableType kExeType = kExeTypeWin32X86;
  enum : uint16_t { kMagic = 0x10B };
  enum : uint16_t { kRelocType = 3 };
  enum : offset_t { kVAWidth = 4 };
  static const char kExeTypeString[];

  using ImageOptionalHeader = pe::ImageOptionalHeader;
  using RelFinder = Rel32FinderX86;
  using Address = uint32_t;
};

struct Win32X64Traits {
  static constexpr ExecutableType kExeType = kExeTypeWin32X64;
  enum : uint16_t { kMagic = 0x20B };
  enum : uint16_t { kRelocType = 10 };
  enum : offset_t { kVAWidth = 8 };
  static const char kExeTypeString[];

  using ImageOptionalHeader = pe::ImageOptionalHeader64;
  using RelFinder = Rel32FinderX64;
  using Address = uint64_t;
};

template <class Traits>
class DisassemblerWin32 : public Disassembler {
 public:
  enum ReferenceType : uint8_t { kReloc, kRel32, kTypeCount };

  // Applies quick checks to determine whether |image| *may* point to the start
  // of an executable. Returns true iff the check passes.
  static bool QuickDetect(ConstBufferView image);

  // Main instantiator and initializer.
  static std::unique_ptr<DisassemblerWin32> Make(ConstBufferView image);

  DisassemblerWin32();
  ~DisassemblerWin32() override;

  // Disassembler:
  ExecutableType GetExeType() const override;
  std::string GetExeTypeString() const override;
  std::vector<ReferenceGroup> MakeReferenceGroups() const override;

  // Functions that return reader / writer for references.
  std::unique_ptr<ReferenceReader> MakeReadRelocs(offset_t lo, offset_t hi);
  std::unique_ptr<ReferenceReader> MakeReadRel32(offset_t lo, offset_t hi);
  std::unique_ptr<ReferenceWriter> MakeWriteRelocs(MutableBufferView image);
  std::unique_ptr<ReferenceWriter> MakeWriteRel32(MutableBufferView image);

 private:
  // Disassembler:
  bool Parse(ConstBufferView image) override;

  // Parses the file header. Returns true iff successful.
  bool ParseHeader();

  // Parsers to extract references. These are lazily called, and return whether
  // parsing was successful (failures are non-fatal).
  bool ParseAndStoreRelocBlocks();
  bool ParseAndStoreRel32();

  // PE-specific translation utilities.
  rva_t AddressToRva(typename Traits::Address address) const;
  typename Traits::Address RvaToAddress(rva_t rva) const;

  // In-memory copy of sections.
  std::vector<pe::ImageSectionHeader> sections_;

  // Image base address to translate between RVA and VA.
  typename Traits::Address image_base_ = 0;

  // Pointer to data Directory entry of the relocation table.
  const pe::ImageDataDirectory* base_relocation_table_ = nullptr;

  // Translator between offsets and RVAs.
  AddressTranslator translator_;

  // Reference storage.
  BufferRegion reloc_region_;
  std::vector<offset_t> reloc_block_offsets_;
  offset_t reloc_end_ = 0;
  std::vector<offset_t> rel32_locations_;

  // Initialization states of reference storage, used for lazy initialization.
  // TODO(huangs): Investigate whether lazy initialization is useful for memory
  // reduction. This is a carryover from Courgette. To be sure we should run
  // experiment after Zucchini is able to do ensemble patching.
  bool has_parsed_relocs_ = false;
  bool has_parsed_rel32_ = false;

  DISALLOW_COPY_AND_ASSIGN(DisassemblerWin32);
};

using DisassemblerWin32X86 = DisassemblerWin32<Win32X86Traits>;
using DisassemblerWin32X64 = DisassemblerWin32<Win32X64Traits>;

}  // namespace zucchini

#endif  // CHROME_INSTALLER_ZUCCHINI_DISASSEMBLER_WIN32_H_
