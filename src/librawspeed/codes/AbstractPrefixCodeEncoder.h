/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2024 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#pragma once

#include "rawspeedconfig.h"
#include "adt/Invariant.h"
#include "codes/AbstractPrefixCodeTranscoder.h"

namespace rawspeed {

template <typename CodeTag>
class AbstractPrefixCodeEncoder : public AbstractPrefixCodeTranscoder<CodeTag> {
public:
  using Base = AbstractPrefixCodeTranscoder<CodeTag>;

  using Tag = typename Base::Tag;
  using Parent = typename Base::Parent;
  using CodeSymbol = typename Base::CodeSymbol;
  using Traits = typename Base::Traits;

  using Base::Base;

  void setup(bool fullDecode_, bool fixDNGBug16_) {
    if (fullDecode_)
      ThrowRSE("We don't currently support full encoding");
    if (fixDNGBug16_)
      ThrowRSE("We don't support handling DNG 1.0 LJpeg bug here");

    Base::setup(fullDecode_, fixDNGBug16_);
  }
};

} // namespace rawspeed
