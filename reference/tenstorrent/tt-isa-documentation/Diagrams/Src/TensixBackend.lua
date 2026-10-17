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

local function HorizConn(out, left, head, right, etc)
  local y = (math.max(left.y, right.y) + math.min(left.bottom, right.bottom)) * 0.5
  Drawing.ThickArrow(out, left.right, y, head, right.x, y, "black", 1.5)
  if etc and etc.label then
    local x = (left.right + right.x) * 0.5
    local anchor = "middle"
    if etc.label_x == "right" then
      x = right.x - 12
      anchor = "end"
    elseif etc.label_x == "left" then
      x = left.right + 12
      anchor = "start"
    end
    out:putf([[<text x="%d" y="%d" text-anchor="%s" dominant-baseline="auto">%s</text>]], x, y - 7, anchor, etc.label)
  end
  return y
end

local function VertConn(out, top, head, bottom, etc)
  local x = (math.max(top.x, bottom.x) + math.min(top.right, bottom.right)) * 0.5
  Drawing.ThickArrow(out, x, top.bottom, head, x, bottom.y, "black", 1.5)
  if etc and etc.label then
    local y = (top.bottom + bottom.y) * 0.5
    out:putf([[<text x="%d" y="%d" text-anchor="%s" dominant-baseline="middle">%s</text>]], x - 7, y + 1, "end", etc.label)
  end
end

local function TensixBackend()
  local out = buffer.new()
  local nul = buffer.new()
  local dims = {w = 945, h = 516}
  out:putf([[<svg version="1.1" width="%u" height="%u" xmlns="http://www.w3.org/2000/svg">]], dims.w, dims.h)
  out:putf([[<rect width="%u" height="%u" rx="15" stroke="transparent" fill="white"/>]], dims.w, dims.h)

  local x_spacing = 28
  local y_spacing = 5
  local fpu = Drawing.RectText(out, {x = 5.5, y = 5.5, w = 90, h = 350, color = xu_color}, {"Matrix Unit", "(FPU)"})
  local sfpu = Drawing.RectText(out, {x = fpu.x, y = fpu.bottom + y_spacing, w = fpu.w, h = 150, color = xu_color}, {"Vector Unit", "(SFPU)"})
  local srcb = Drawing.RectText(out, {x = fpu.right + x_spacing, y = fpu.y, w = 120, h = 70, color = data_color}, {"SrcB", "2x 64x 16x 19b"})
  local srca = Drawing.RectText(out, {x = srcb.x, y = srcb.bottom + y_spacing, w = srcb.w, h = srcb.h, color = data_color}, {"SrcA", "2x 64x 16x 19b"})
  local lreg = Drawing.RectText(out, {x = sfpu.right + x_spacing, bottom = sfpu.bottom, w = srcb.w, h = 70, color = data_color}, {"LReg", "9x 32x 32b", "and 4x 8x 32b"})
  local dst = Drawing.RectText(out, {x = srca.x, y = srca.bottom + y_spacing, w = srca.w, bottom = lreg.y - y_spacing, color = data_color}, {"Dst", "1024x 16x 16b", "or 512x 16x 32b"})
  HorizConn(out, fpu, "<>", srcb)
  HorizConn(out, fpu, "<>", srca)
  HorizConn(out, fpu, "<>", dst)
  HorizConn(out, sfpu, "<>", dst)
  HorizConn(out, sfpu, "<>", lreg)

  local unp0_mux = Drawing.Mux(out, {x = srca.right + x_spacing, y_middle = (srca.bottom + dst.y) * 0.5, w = 10, h = 70}, ">")
  HorizConn(out, srca, "<-", unp0_mux)
  HorizConn(out, dst, "<-", unp0_mux)

  local unp0 = Drawing.RectText(nul, {x = unp0_mux.right + x_spacing, y_middle = unp0_mux.y_middle, w = 90, h = 70, color = xu_color}, {"Unpacker 0"})
  local unp_extra_y = 18
  Drawing.RectText(out, {x = unp0.x, bottom = unp0.bottom - 1, w = unp0.w, h = unp0.h + unp_extra_y, color = xu_color}, {"Unpacker 0"})
  HorizConn(out, unp0_mux, "--", unp0)

  local unp1 = Drawing.RectText(nul, {x = unp0.x, y_middle = srcb.y_middle, w = unp0.w, h = unp0.h, color = xu_color}, {"Unpacker 1"})
  Drawing.RectText(out, {x = unp1.x, y = unp1.y, w = unp1.w, h = unp1.h + unp_extra_y, color = xu_color}, {"Unpacker 1"})
  HorizConn(out, srcb, "<-", unp1)

  local unp_mux = Drawing.Mux(out, {x = unp0.right + x_spacing, y = unp1.bottom - 20, bottom = unp0.y + 20, w = 10}, ">")
  HorizConn(out, unp1, "<-", unp_mux)
  HorizConn(out, unp0, "<-", unp_mux)

  local l1 = Drawing.RectText(out, {x = unp_mux.right + x_spacing * 2, y = fpu.y, bottom = sfpu.bottom, w = 90, color = data_color}, {"L1", "1464 KiB"})
  HorizConn(out, unp1, "<-", l1, {label = "128b", label_x = "right"})
  HorizConn(out, unp_mux, "--", l1, {label = "384b", label_x = "right"})
  HorizConn(out, unp0, "<-", l1, {label = "128b", label_x = "right"})

  local pack0_mux = Drawing.Mux(out, {x = dst.right + x_spacing, y_middle = (dst.bottom + lreg.y) * 0.5, w = 10, h = 70}, ">")
  local pack0_mux_y0 = HorizConn(out, dst, "--", pack0_mux)

  local pack0 = Drawing.RectText(out, {x = pack0_mux.right + x_spacing, y_middle = pack0_mux.y_middle, w = unp0.w, h = 60, color = xu_color}, {"Packer 0"})
  HorizConn(out, pack0_mux, "->", pack0)
  HorizConn(out, pack0, "<>", l1, {label = "128b", label_x = "right"})

  local pack0_mux_y1 = pack0.y_middle + pack0.h + y_spacing
  Drawing.MultiLine(out, {pack0_mux.x, pack0_mux.bottom - (pack0_mux_y0 - pack0_mux.y), "<", (pack0_mux.x + lreg.right) * 0.5, "v", pack0_mux_y1, ">", l1.x, head = false, stroke_width = 6})
  out:putf([[<text x="%d" y="%d" text-anchor="%s" dominant-baseline="auto">%s</text>]], l1.x - 12, pack0_mux_y1 - 7, "end", "128b")

  local packers = {[0] = pack0}
  for i = 1, 3 do
    packers[i] = Drawing.RectText(out, {x = pack0.x, bottom = packers[i - 1].y - y_spacing, w = pack0.w, h = pack0.h, color = xu_color}, {"Packer ".. i})
    HorizConn(out, dst, "->", packers[i])
    HorizConn(out, packers[i], "<>", l1, {label = "128b", label_x = "right"})
  end

  local mover = Drawing.RectText(out, {x = l1.right + x_spacing * 2, bottom = l1.bottom, w = 100, h = 70, color = xu_color}, {"Mover"})
  local noc0 = Drawing.RectText(out, {x = mover.x, y = l1.y, w = mover.w, h = mover.h, color = "white"}, {"NoC 0", "Data"})
  local noc1 = Drawing.RectText(out, {x = mover.x, y = noc0.bottom + y_spacing, w = mover.w, h = mover.h, color = "white"}, {"NoC 1", "Data"})
  for _, box in ipairs{mover, noc0, noc1} do
    local width = (box == mover) and "128b" or "256b"
    HorizConn(out, l1, "->", {x = box.x, y = box.y, bottom = box.y_middle}, {label = width, label_x = "left"})
    HorizConn(out, l1, "<-", {x = box.x, y = box.y_middle, bottom = box.bottom}, {label = width, label_x = "left"})
  end

  local thcon = Drawing.RectText(out, {x = mover.x, bottom = mover.y - y_spacing, w = mover.w, h = mover.h, color = xu_color}, {"Scalar Unit", "(ThCon)"})
  HorizConn(out, l1, "<>", thcon, {label = "128b", label_x = "left"})

  local y_spacing_gprs = 45
  local gprs = Drawing.RectText(out, {x = thcon.x, bottom = thcon.y - y_spacing_gprs, w = thcon.w, h = 50, color = data_color}, {"GPRs", "3x 64x 32b"})
  VertConn(out, gprs, "<>", thcon, {label = "128b"})

  local gprs_rv_x = gprs.right + 36
  Drawing.MultiLine(out, {gprs.right - 10, gprs.y_middle, ">", gprs_rv_x, head = "both"})
  for k, text in ipairs{"RISCV B / T0 / T1 / T2", '<tspan font-family="monospace">REGFILE_BASE</tspan>'} do
    out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="middle">%s</text>]], gprs_rv_x + 6, gprs.y_middle + (k - 1.5) * 20 + 2, text)
  end

  local cfgu = Drawing.RectText(out, {x = thcon.x, bottom = gprs.y - y_spacing_gprs, w = thcon.w, h = thcon.h, color = xu_color}, {"Configuration", "Unit"})
  VertConn(out, cfgu, "<>", gprs, {label = "128b"})

  out:putf"</svg>\n"
  return tostring(out)
end

assert(io.open(own_dir .."../Out/TensixBackend.svg", "w")):write(TensixBackend{})
