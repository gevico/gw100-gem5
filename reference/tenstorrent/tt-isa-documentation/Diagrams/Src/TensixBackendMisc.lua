#!/usr/bin/env luajit
-- SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
--
-- SPDX-License-Identifier: Apache-2.0

local buffer = require"string.buffer"
local data_color = "#f4d7e3"
local xu_color = "#d4aa00"
local fe_color = "#87deaa"
local insn_color = "#aaaaff"

local own_dir = debug.getinfo(1, "S").source:match"@?(.*/)" or ""
package.path = own_dir .."?.lua;".. package.path
local Drawing = require"Drawing"

local function HorizConn(out, left, head, right, y)
  y = y or (math.max(left.y, right.y) + math.min(left.bottom, right.bottom)) * 0.5
  if head == "<-" then
    Drawing.MultiLine(out, {right.x - 2, y, "<", left.right + 2})
  else
    Drawing.MultiLine(out, {left.right + 2, y, ">", right.x - 2, head = head == "<>" and "both" or nil})
  end
end

local function VertConn(out, top, head, bottom)
  local x = (math.max(top.x, bottom.x) + math.min(top.right, bottom.right)) * 0.5
  if head == "<-" then
    Drawing.MultiLine(out, {x, bottom.y - 2, "^", top.bottom + 2})
  else
    Drawing.MultiLine(out, {x, top.bottom + 2, "v", bottom.y - 2, head = head == "<>" and "both" or nil})
  end
end

local function WireTextRight(out, x, y, text)
  local x2 = x + 20
  local x3 = x2 + 4
  Drawing.MultiLine(out, {x, y, ">", x2})
  out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="middle">%s</text>]], x3, y + 1, text)
end

local function TensixBackendMisc()
  local out = buffer.new()
  local nul = buffer.new()
  local dims = {w = 1045, h = 511}
  out:putf([[<svg version="1.1" width="%u" height="%u" xmlns="http://www.w3.org/2000/svg">]], dims.w, dims.h)
  out:putf([[<rect width="%u" height="%u" rx="15" stroke="transparent" fill="white"/>]], dims.w, dims.h)

  local x_spacing = 28
  local y_spacing = 28
  local rwc_f = Drawing.RectText(out, {x = 5.5, y = 5.5, w = 170, h = 50, color = data_color}, {"Fidelity Phase RWCs", "3x 2b"})
  local fpu = Drawing.RectText(out, {x = rwc_f.x, y = rwc_f.bottom + y_spacing, w = rwc_f.w, h = rwc_f.h * 2 + y_spacing, color = xu_color}, {"Matrix Unit", "(FPU)"})
  local rwc = Drawing.RectText(out, {x = fpu.x, y = fpu.bottom + y_spacing, w = fpu.w, h = rwc_f.h, color = data_color}, {"Dst, SrcA, SrcB RWCs", "3x 2x (10b + 6b + 6b)"})
  local sfpu = Drawing.RectText(out, {x = rwc.x, y = rwc.bottom + y_spacing, w = rwc.w, h = rwc_f.h * 2 + 10, color = xu_color}, {"Vector Unit", "(SFPU)"})
  local cc = Drawing.RectText(out, {x = sfpu.x, y = sfpu.bottom + y_spacing, w = sfpu.w, h = rwc.h, color = data_color}, {"Lane Predication Masks", "32x 9x (1b + 1b)"})

  VertConn(out, rwc_f, "<>", fpu)
  VertConn(out, fpu, "<>", rwc)
  VertConn(out, rwc, "<>", sfpu)
  VertConn(out, sfpu, "<>", cc)

  local acl = Drawing.RectText(out, {x = fpu.right + x_spacing, y = fpu.y, w = 160, h = rwc_f.h, color = data_color}, {"SrcA, SrcB Access", "Control; 2x 4b"})
  local adc = Drawing.RectText(out, {x = acl.right + x_spacing, y = acl.bottom + y_spacing, w = 130, h = acl.h, color = data_color}, {"Unpacker ADCs", "3x 2x 2x 2x 47b"})
  local adc1 = Drawing.RectText(out, {x = adc.x, y = adc.bottom + 10, w = adc.w, h = adc.h, color = data_color}, {"Packer ADCs", "3x 1x 2x 2x 47b"})
  local misc = Drawing.RectText(out, {x = acl.x, y = adc.y, w = acl.w, bottom = adc1.bottom, color = xu_color}, {"Miscellaneous", "Unit"})

  local scalar = Drawing.RectText(out, {x = adc.right + x_spacing, y = adc.y, w = 128, h = misc.h, color = xu_color}, {"Scalar Unit", "(ThCon)"})
  local unpackers = Drawing.RectText(out, {x = adc.x, y = acl.y, right = scalar.right, h = acl.h, color = xu_color}, {"Unpackers"})
  local packers = Drawing.RectText(out, {x = adc1.x, y = adc1.bottom + y_spacing, right = scalar.right, h = unpackers.h, color = xu_color}, {"Packers"})
  local meta_fifos = Drawing.RectText(out, {right = packers.right, y = packers.bottom + y_spacing, w = scalar.w, h = packers.h, color = data_color}, {"Metadata FIFOs", "4x 4x (16b + 32b)"})
  local unpack_row = Drawing.RectText(out, {x = unpackers.x, y = rwc_f.y, w = (unpackers.w - 10) * 0.5, h = rwc_f.h, color = data_color}, {"SrcA, SrcB Row", "Offset; 2x 3x 2b"})
  local unpack_ctx = Drawing.RectText(out, {right = unpackers.right, y = rwc_f.y, w = (unpackers.w - 10) * 0.5, h = rwc_f.h, color = data_color}, {"Context Counters", "2x 3x 3b"})

  VertConn(out, unpack_row, "<>", unpackers)
  VertConn(out, unpack_ctx, "<>", unpackers)
  HorizConn(out, fpu, "<>", acl)
  HorizConn(out, acl, "<>", unpackers)
  VertConn(out, acl, "<-", misc)
  HorizConn(out, misc, "->", adc)
  HorizConn(out, misc, "->", adc1)
  VertConn(out, unpackers, "<>", adc)
  VertConn(out, adc1, "<>", packers)
  HorizConn(out, adc, "<-", scalar)
  HorizConn(out, adc1, "<-", scalar)
  VertConn(out, scalar, "<-", packers)
  VertConn(out, packers, "->", meta_fifos)
  local meta_fifos_out_y = meta_fifos.bottom + y_spacing
  Drawing.MultiLine(out, {meta_fifos.x_middle, meta_fifos.bottom + 2, "v", meta_fifos_out_y - 2})
  out:putf([[<text x="%d" y="%d" text-anchor="middle" dominant-baseline="hanging">To TDMA-RISC</text>]], meta_fifos.x_middle, meta_fifos_out_y + 2)

  local sfpu_cfg0 = Drawing.RectText(out, {x = sfpu.right + x_spacing, y = sfpu.y, w = acl.w, h = rwc_f.h, color = data_color}, {"Lane Configuration", "8x 18b"})
  local sfpu_cfg1 = Drawing.RectText(out, {x = sfpu.right + x_spacing, bottom = sfpu.bottom, w = acl.w, h = rwc_f.h, color = data_color}, {'<tspan font-family="monospace">SFPLOADMACRO</tspan>', "Configuration; 8x 268b"})
  HorizConn(out, sfpu, "<>", sfpu_cfg0)
  HorizConn(out, sfpu, "<>", sfpu_cfg1)

  local mux_y_pad = 15
  local mux_in = Drawing.Mux(out, {x = scalar.right + x_spacing, y = unpackers.y_middle - mux_y_pad, w = 10, bottom = packers.y_middle + mux_y_pad}, ">")
  HorizConn(out, unpackers, "->", mux_in, unpackers.y_middle)
  HorizConn(out, scalar, "<>", mux_in)
  HorizConn(out, packers, "->", mux_in, packers.y_middle)

  local thcon_mux = Drawing.Mux(out, {x = mux_in.right + x_spacing, y_middle = mux_in.y_middle, w = 10, h = 5 * 24 + 2 * 15}, "<")
  Drawing.LineTextAbove(out, {mux_in.right, mux_in.y_middle}, {thcon_mux.x, mux_in.y_middle})

  local y = thcon_mux.y + 15
  for _, target in ipairs{"NoC 0 Configuration / Command", "NoC 1 Configuration / Command", "NoC Overlay Configuration / Command", "TDMA-RISC Configuration / Command", "PIC Configuration / Status", "Tile Control / Debug / Status"} do
    WireTextRight(out, thcon_mux.right, y, target)
    y = y + 24
  end

  out:putf"</svg>\n"
  return tostring(out)
end

assert(io.open(own_dir .."../Out/TensixBackendMisc.svg", "w")):write(TensixBackendMisc{})
