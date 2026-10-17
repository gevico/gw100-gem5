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

local function VChain(out, boxes)
  for i = 2, #boxes do
    local a = boxes[i - 1]
    local b = boxes[i]
    local x = (math.max(a.x, b.x) + math.min(a.right, b.right)) * 0.5
    Drawing.MultiLine(out, {x, a.bottom + 1, "v", b.y - 2})
  end
end

local function VChain3(out, boxes)
  for i = 2, #boxes do
    local a = boxes[i - 1]
    local b = boxes[i]
    local x = (math.max(a.x, b.x) + math.min(a.right, b.right)) * 0.5
    for dx = -1, 1 do
      Drawing.MultiLine(out, {x + dx * 15, a.bottom + 1, "v", b.y - 2})
    end
  end
end

local function UnpackerPipeline()
  local out = buffer.new()
  local nul = buffer.new()
  local dims = {w = 836, h = 715}
  out:putf([[<svg version="1.1" width="%u" height="%u" xmlns="http://www.w3.org/2000/svg">]], dims.w, dims.h)
  out:putf([[<rect width="%u" height="%u" rx="15" stroke="transparent" fill="white"/>]], dims.w, dims.h)

  local y_spacing = 20
  local xu_h = 42
  local mux_pad = 15

  local iag = {}
  iag[1] = Drawing.RectText(out, {x = 5.5, w = 320, y = 5.5, h = xu_h, color = xu_color}, {"Input Address Generator and Datum Count"})
  iag[2] = Drawing.RectText(out, {right = dims.w - 5.5, w = iag[1].w, y = iag[1].y, h = iag[1].h, color = xu_color}, {"Input Address Generator and Datum Count"})
  local rsi = Drawing.RectText(out, {x = iag[1].right + y_spacing, right = iag[2].x - y_spacing, y = iag[1].y, h = iag[1].h, color = data_color}, {"RSI Cache", "(for decompression)"})
  Drawing.MultiLine(out, {rsi.x - 1, rsi.y_middle, "<", iag[1].right + 2})
  Drawing.MultiLine(out, {rsi.right + 1, rsi.y_middle, ">", iag[2].x - 2})

  local l1 = Drawing.RectText(out, {x = iag[1].x, right = iag[2].right, h = 50, y = iag[1].bottom + y_spacing, color = data_color}, {"Input from L1"})
  Drawing.MultiLine(out, {rsi.x_middle, l1.y - 1, "^", rsi.bottom + 2})

  local l1_streams = {}
  for i, label in ipairs{"RLE Stream", "Exponent Stream", "Datum Stream", "Datum Stream", "Exponent Stream", "RLE Stream"} do
    local x = i == 1 and l1.x or l1_streams[i - 1].right + 10
    if i == 4 then x = x + 7 end
    l1_streams[i] = Drawing.RectText(out, {x = x, w = 128, h = 32, y = l1.bottom + y_spacing * 2 + 10, color = data_color}, {label})
    if i < 3 or i > 4 then
      VChain(out, {l1, l1_streams[i]})
    end
    local iag = iag[i <= 3 and 1 or 2]
    x = (math.max(iag.x, l1_streams[i].x) + math.min(iag.right, l1_streams[i].right)) * 0.5
    Drawing.MultiLine(out, {x, iag.bottom + 1, "v", l1.y - 2})
    if i == 3 or i == 4 then
      Drawing.MultiLine(out, {x, l1.bottom + 1, "v", l1_streams[i].y - 2})
    end
  end
  local data_mux = Drawing.Mux(out, {x = l1_streams[3].x_middle, right = l1_streams[4].x_middle, y = l1.bottom + y_spacing, h = 10}, "^")
  VChain3(out, {l1, data_mux, l1_streams[3]})
  VChain3(out, {data_mux, l1_streams[4]})

  local fc = {}
  for i = 1, 2 do
    local j = i * 2
    fc[i] = Drawing.RectText(out, {x = l1_streams[j].x, right = l1_streams[j+1].right, y = l1_streams[j].bottom + y_spacing, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Format Conversion"})
    VChain(out, {l1_streams[j], fc[i]})
    VChain(out, {l1_streams[j + 1], fc[i]})
  end
  local decomp = {}
  for i = 1, 2 do
    local j = i * 4 - 3
    local w = l1_streams[j+1].right - l1_streams[j].x
    local x = l1_streams[j].x + (1.5 - i) * l1_streams[1].w * 0.5
    decomp[i] = Drawing.RectText(out, {x = x, w = w, y = fc[i].bottom + y_spacing, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Decompress (Optional)"})
  end
  VChain(out, {l1_streams[1], decomp[1]})
  VChain(out, {l1_streams[#l1_streams], decomp[2]})

  local upsample = {}
  for i = 1, 2 do
    upsample[i] = Drawing.RectText(out, {x_middle = decomp[i].x_middle, w = (decomp[i].w + l1_streams[1].w) * 0.5, y = decomp[i].bottom + y_spacing, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Upsample (Optional)"})
  end

  local wait = {}
  for i, src in ipairs{"SrcA", "SrcB"} do
    local j = i * 3 - 1
    wait[i] = Drawing.RectText(out, {x_middle = upsample[i].x_middle, w = l1_streams[j].w, y = upsample[i].bottom + y_spacing, h = 50, color = xu_color}, {"Wait for", [[<tspan font-family="monospace">]].. src ..[[</tspan>]]})
    VChain(out, {fc[i], decomp[i], upsample[i], wait[i]})
  end

  local dst_mux = Drawing.Mux(out, {x = l1_streams[1].x, right = l1_streams[3].x_middle, y = wait[1].bottom + y_spacing, h = 10}, "^")

  local transpose = Drawing.RectText(out, {x = dst_mux.x_middle + 5, right = dst_mux.right, y = dst_mux.bottom + y_spacing, h = xu_h, color = xu_color}, {"Transpose", "(Optional)"})

  local out_dst = Drawing.RectText(out, {x = dst_mux.x, w = transpose.w, y = transpose.bottom + y_spacing, h = l1.h, color = data_color}, {"Output to", [[<tspan font-family="monospace">Dst</tspan>]]})
  local out_srca = Drawing.RectText(out, {right = dst_mux.right, w = transpose.w, y = transpose.bottom + y_spacing, h = l1.h, color = data_color}, {"Output to", [[<tspan font-family="monospace">SrcA</tspan>]]})
  local out_srcb = Drawing.RectText(out, {x_middle = wait[2].x_middle, w = transpose.w, y = transpose.bottom + y_spacing, h = l1.h, color = data_color}, {"Output to", [[<tspan font-family="monospace">SrcB</tspan>]]})

  VChain(out, {wait[1], dst_mux, out_dst})
  VChain(out, {dst_mux, transpose, out_srca})
  VChain(out, {wait[2], out_srcb})

  local oag_mux = Drawing.Mux(out, {x = dst_mux.x, w = dst_mux.w, y = out_dst.bottom + y_spacing, h = 10}, "v")
  for _, box in ipairs{out_dst, out_srca} do
    Drawing.MultiLine(out, {box.x_middle, oag_mux.y - 1, "^", box.bottom + 2})
  end
  local oag = {}
  for i = 1, 2 do
    oag[i] = Drawing.RectText(out, {x_middle = wait[i].x_middle, w = decomp[i].w, y = oag_mux.bottom + y_spacing, h = xu_h, color = xu_color}, {"Output Address Generator"})
    Drawing.MultiLine(out, {oag[i].x_middle, oag[i].y - 1, "^", (i == 1 and oag_mux or out_srcb).bottom + 2})
  end

  local wrreg = Drawing.RectText(out, {x_middle = (dst_mux.right + out_srcb.x) * 0.5, w = 105, y = dst_mux.y, h = 46, color = xu_color}, {"Register", "Write"})

  out:putf"</svg>\n"
  return tostring(out)
end

assert(io.open(own_dir .."../Out/UnpackerPipeline.svg", "w")):write(UnpackerPipeline{})
