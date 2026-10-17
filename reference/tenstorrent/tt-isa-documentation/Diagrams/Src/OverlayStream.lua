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

local function OverlayStream()
  local out = buffer.new()
  local dims = {w = 930, h = 735}
  out:putf([[<svg version="1.1" width="%u" height="%u" xmlns="http://www.w3.org/2000/svg">]], dims.w, dims.h)
  out:putf([[<rect width="%u" height="%u" rx="15" stroke="transparent" fill="white"/>]], dims.w, dims.h)

  local function v_chain(components)
    local dir = components[0] or "v"
    local from_pad = 1
    local to_pad = 2
    local x
    for i = 2, #components do
      local from = components[i - 1]
      local to = components[i]
      x = (math.max(from.x, to.x) + math.min(from.right, to.right)) * 0.5
      if dir == "v" then
        Drawing.MultiLine(out, {x, from.bottom + from_pad, "v", to.y - to_pad})
      else
        Drawing.MultiLine(out, {x, to.y - from_pad, "^", from.bottom + to_pad})
      end
    end
    return x
  end
  local function h_chain(components)
    local dir = components[0] or ">"
    local from_pad = 1
    local to_pad = 2
    for i = 2, #components do
      local from = components[i - 1]
      local to = components[i]
      local y = (math.max(from.y, to.y) + math.min(from.bottom, to.bottom)) * 0.5
      if to.rhombus then y = to.y_middle end
      if from.rhombus then y = from.y_middle end
      if dir == ">" then
        Drawing.MultiLine(out, {from.right + from_pad, y, ">", to.x - to_pad})
      else
        Drawing.MultiLine(out, {to.x - from_pad, y, "<", from.right + to_pad})
      end
    end
  end
  local function data_chain(components)
    for i = 2, #components do
      local from = components[i - 1]
      local to = components[i]
      local y = (math.max(from.y, to.y) + math.min(from.bottom, to.bottom)) * 0.5
      if to.mux then
        Drawing.ThickArrow(out, from.right + 0.5, y, "--", to.x - 0.5, y, insn_color, 1)
      else
        Drawing.ThickArrow(out, from.right + 0.5, y, "->", to.x - 1, y, insn_color, 1)
      end
    end
  end

  local y_spacing = 20
  local x_spacing = 20
  local xu_h = 46

  local idle = Drawing.RectText(out, {x = 114.5, y = 5.5, w = 180, h = xu_h, h_text = xu_h + 2, color = xu_color}, "Stream idle")
  local rv_cfg = Drawing.RectText(out, {x = idle.x, y = idle.bottom + y_spacing, w = idle.w, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Software sets", "configuration"})
  local rv_start = Drawing.RectText(out, {x = rv_cfg.x, y = rv_cfg.bottom + y_spacing, w = rv_cfg.w, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Software starts", "the phase"})
  local flush = Drawing.RectText(out, {x = rv_start.x, y = rv_start.bottom + y_spacing, w = rv_start.w, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Wait for previous phase", "L1 reads to complete"})

  local any_msgs = Drawing.RectText(out, {x_middle = flush.x_middle, w = 180, y = flush.bottom + y_spacing, h = 130, rhombus = true, color = fe_color}, {"Expecting to", "handle any messages", "this phase?"})

  local pre_recv = Drawing.RectText(out, {x = flush.x, y = any_msgs.bottom + y_spacing, w = flush.w, h = xu_h, h_text = xu_h + 2, color = xu_color}, {"Handshake with", "configured source"})

  local header_array = Drawing.RectText(out, {x = pre_recv.right + x_spacing, y = pre_recv.bottom + y_spacing, w = 120, h = xu_h, h_text = xu_h + 2, color = data_color}, {"Message header", "array (in L1)"})
  local meta_fifo = Drawing.RectText(out, {x = header_array.right + x_spacing, y = header_array.y, w = 130, h = xu_h, h_text = xu_h + 2, color = data_color}, {"Message metadata", "FIFO (in stream)"})
  local recv_buf = Drawing.RectText(out, {x = header_array.x, y = header_array.bottom + y_spacing, right = meta_fifo.right, h = xu_h, h_text = xu_h + 2, color = data_color}, {"Receive buffer", "FIFO (in L1)"})

  local recv_one = Drawing.RectText(out, {x = pre_recv.x, y_middle = (header_array.y_middle + recv_buf.y_middle) * 0.5, w = pre_recv.w, h = xu_h * 1.75, h_text = xu_h * 1.75 + 2, color = xu_color}, {"Receive configured", "number of messages", "from configured source"})

  local tx_one = Drawing.RectText(out, {x = recv_buf.right + x_spacing, y = recv_one.y, w = recv_one.w, h = recv_one.h, h_text = recv_one.h_text, color = xu_color}, {"Transmit configured", "number of messages to", "configured destination"})
  local pre_tx = Drawing.RectText(out, {x = tx_one.x, y = pre_recv.y, w = tx_one.w, h = pre_recv.h, h_text = pre_recv.h_text, color = xu_color}, {"Handshake with", "configured destination"})
  local l1_fifo = Drawing.RectText(out, {x_middle = (recv_buf.right + tx_one.x) * 0.5, y = recv_buf.bottom + y_spacing, w = meta_fifo.w, h = meta_fifo.h, h_text = meta_fifo.h_text, color = data_color}, {"L1 read complete", "FIFO (in stream)"})

  local partial_flush = Drawing.RectText(out, {x = tx_one.x, y = l1_fifo.bottom + y_spacing, w = flush.w, h = flush.h, h_text = flush.h_text, color = xu_color}, {"Wait for destination", "mostly (or entirely) done"})

  out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="auto">No</text>]], any_msgs.right + 5, any_msgs.y_middle - 4)

  local y_pad_mux = 15
  local y_space_mux = 24
  local mux_labels = {"Software", "NoC", "Ethernet", "Gather in"}
  local mux_y1 = recv_one.y_middle - (#mux_labels - 1) * y_space_mux * 0.5
  local mux_yN = mux_y1 + (#mux_labels - 1) * y_space_mux
  local recv_mux = Drawing.Mux(out, {right = recv_one.x - x_spacing, y = mux_y1 - y_pad_mux, bottom = mux_yN + y_pad_mux, w = 10}, ">")
  for i, label in ipairs(mux_labels) do
    local x = recv_mux.x - 10
    local y = mux_y1 + (i - 1) * y_space_mux
    out:putf([[<text x="%d" y="%d" text-anchor="end" dominant-baseline="middle">%s</text>]], x - 1, y + 2, label)
    data_chain{{y = y, bottom = y, right = x}, recv_mux}
  end

  mux_labels[#mux_labels] = "Gather out"
  local tx_mux = Drawing.Mux(out, {x = tx_one.right + x_spacing, y = mux_y1 - y_pad_mux, bottom = mux_yN + y_pad_mux, w = 10}, "<")
  for i, label in ipairs(mux_labels) do
    local x = tx_mux.right + 15
    local y = mux_y1 + (i - 1) * y_space_mux
    out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="middle">%s</text>]], x + 1, y + 2, label)
    data_chain{tx_mux, {y = y, bottom = y, x = x}}
  end

  Drawing.MultiLine(out, {recv_one.x_middle, recv_one.bottom + 1, "v", partial_flush.y_middle, ">", partial_flush.x - 2})

  do
    local hs_width = 35
    local y0 = pre_recv.y + pre_recv.h / 4
    local y1 = pre_recv.bottom - pre_recv.h / 4
    Drawing.ThickArrow(out, pre_recv.x - hs_width, y0, "<-", pre_recv.x - 0.5, y0, insn_color, 1)
    Drawing.ThickArrow(out, pre_recv.x - hs_width, y1, "->", pre_recv.x - 1, y1, insn_color, 1)

    Drawing.ThickArrow(out, pre_tx.right + 1, y0, "<-", pre_tx.right + hs_width, y0, insn_color, 1)
    Drawing.ThickArrow(out, pre_tx.right + 0.5, y1, "->", pre_tx.right + hs_width, y1, insn_color, 1)

    local hs_cache = Drawing.RectText(out, {x_middle = pre_tx.right, bottom = pre_tx.y - y_spacing, w = meta_fifo.w, h = meta_fifo.h, h_text = meta_fifo.h_text, color = data_color}, {"Handshake cache", "(in overlay)"})
    local x0 = (hs_cache.x + pre_tx.right) * 0.5
    local x1 = pre_tx.right + 20
    Drawing.ThickArrow(out, x0, hs_cache.bottom + 0.5, "->", x0, pre_tx.y - 1, insn_color, 1)
    Drawing.ThickArrow(out, x1, hs_cache.bottom + 1, "<-", x1, y0, insn_color, 1)
  end

  v_chain{idle, rv_cfg, rv_start, flush, any_msgs, pre_recv, recv_one}
  Drawing.MultiLine(out, {any_msgs.x_middle, any_msgs.bottom + 5, ">", pre_tx.x_middle, "v", pre_tx.y - 2})
  out:putf([[<text x="%d" y="%d" text-anchor="end" dominant-baseline="middle">Yes</text>]], any_msgs.x_middle - 4, any_msgs.bottom + 7)

  local tx_v_chain_x = v_chain{pre_tx, tx_one, l1_fifo}
  v_chain{tx_one, {x = tx_one.x, right = tx_one.right, y = partial_flush.y}}
  v_chain{[0] = "^", recv_buf, l1_fifo}
  data_chain{recv_mux, recv_one, recv_buf, tx_one, tx_mux}
  data_chain{recv_one, header_array, meta_fifo, tx_one}
  Drawing.ThickArrow(out, l1_fifo.x - 0.5 - x_spacing, l1_fifo.y_middle, "<-", l1_fifo.x - 0.5, l1_fifo.y_middle, insn_color, 1)
  out:putf([[<text x="%d" y="%d" text-anchor="end" dominant-baseline="middle">Flow control to source</text>]], l1_fifo.x - 1.5 - x_spacing - 1, l1_fifo.y_middle + 1)

  do
    local fc_x = tx_mux.x - 80
    out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="middle">Flow control from destination</text>]], fc_x, l1_fifo.y_middle + 1)
    local fc_x1 = fc_x - 10
    Drawing.ThickArrow(out, fc_x1, l1_fifo.y_middle - 1, "--", fc_x - 2, l1_fifo.y_middle - 1, insn_color, 1)
    Drawing.ThickArrow(out, fc_x1, tx_one.bottom + 2, "<>", fc_x1, partial_flush.y - 2, insn_color, 1)
  end

  local auto_start = Drawing.RectText(out, {x = rv_start.right + x_spacing, y_middle = rv_start.y_middle, w = any_msgs.w, h = any_msgs.h, rhombus = true, color = fe_color}, {"Configured to", "automatically", "start phase?"})
  local l1_cfg = Drawing.RectText(out, {x = auto_start.right + x_spacing, y_middle = auto_start.y_middle, w = rv_cfg.w, h = rv_cfg.h, h_text = rv_cfg.h_text, color = xu_color}, {"Load configuration", "for phase from L1"})
  local auto_cfg = Drawing.RectText(out, {x = l1_cfg.right + x_spacing, y_middle = l1_cfg.y_middle, w = any_msgs.w, h = any_msgs.h, rhombus = true, color = fe_color}, {"Has pointer", "to configuration for", "next phase?"})

  for i, box in ipairs{auto_start, auto_cfg} do
    out:putf([[<text x="%d" y="%d" text-anchor="end" dominant-baseline="auto">%s</text>]], box.x + 8, box.y_middle - 10, i == 1 and "No" or "Yes")
  end
  out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="auto">No</text>]], auto_cfg.x_middle + 4, auto_cfg.y - 5)
  out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="hanging">Yes</text>]], auto_start.x_middle + 4, auto_start.bottom + 5)

  h_chain{[0] = "<", rv_start, auto_start, l1_cfg, auto_cfg}

  Drawing.MultiLine(out, {partial_flush.right + 1, partial_flush.y_middle, ">", dims.w - 5.5, "^", auto_cfg.y_middle, "<", auto_cfg.right + 2})
  Drawing.MultiLine(out, {any_msgs.right + 1, any_msgs.y_middle, ">", auto_cfg.x_middle, "^", auto_cfg.bottom + 2})
  Drawing.MultiLine(out, {auto_cfg.x_middle, auto_cfg.y - 1, "^", idle.y_middle, "<", idle.right + 2})
  Drawing.MultiLine(out, {auto_start.x_middle, auto_start.bottom + 1, "v", flush.bottom - flush.h / 4, "<", flush.right + 2})

  Drawing.MultiLine(out, {rv_cfg.right + 1, rv_cfg.y + rv_cfg.h / 4, ">", l1_cfg.x_middle, "v", l1_cfg.y - 2})
  out:putf([[<text x="%d" y="%d" text-anchor="start" dominant-baseline="auto">Optional</text>]], rv_cfg.right + 5, rv_cfg.y + rv_cfg.h / 4 - 6)

  out:putf"</svg>\n"
  return tostring(out)
end

assert(io.open(own_dir .."../Out/OverlayStream.svg", "w")):write(OverlayStream{})
