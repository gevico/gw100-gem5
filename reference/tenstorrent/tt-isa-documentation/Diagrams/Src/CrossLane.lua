#!/usr/bin/env luajit
-- SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
--
-- SPDX-License-Identifier: Apache-2.0

local buffer = require"string.buffer"

local own_dir = debug.getinfo(1, "S").source:match"@?(.*/)" or ""
package.path = own_dir .."?.lua;".. package.path
local Drawing = require"Drawing"

local function id(...) return ... end
local function dst_even(r, c) if c % 2 == 0 then return r, (c / 2) end end
local function dst_odd(r, c) if c % 2 == 1 then return r, ((c - 1) / 2) end end
local function hsl(h, s, l)
  local a = s * math.min(l, 1 - l)
  local t = {0, 8, 4}
  for i, n in ipairs(t) do
    local k = (n + h * 12) % 12
    local rgb = l - a * math.max(-1, math.min(k - 3, 9 - k, 1))
    t[i] = ("%02x"):format(rgb * 255)
  end
  return "#".. t[1] .. t[2] .. t[3]
end
local chars = "ABCDEFGHIJKLMNOP"

local ctx_mt = {__index = {
  Grid = function(ctx, args)
    local x = ctx.margin
    local cw = math.floor((ctx.w - x) / 16) * (16 / args.cols)
    local ch = 26
    local ctx_rows = args.ctx_rows or 0
    local rows = args.rows or 4
    local lane_transform = args.lane_transform or id
    local grid_mask = ""
    if ctx_rows > 0 then
      local def_id = (ctx.def_id or 0) + 1
      ctx.def_id = def_id
      ctx.out:putf('<defs><linearGradient id="lg%u" x1="0%%" y1="0%%" x2="0%%" y2="100%%">\n',
        def_id)
      ctx.out:putf('<stop offset="0%%" stop-color="black"/>\n')
      ctx.out:putf('<stop offset="%u%%" stop-color="white"/>\n', math.floor(100 * ctx_rows / (rows + ctx_rows * 2)))
      ctx.out:putf('<stop offset="%u%%" stop-color="white"/>\n', math.ceil(100 * (rows + ctx_rows) / (rows + ctx_rows * 2)))
      ctx.out:putf('<stop offset="100%%" stop-color="black"/>\n')
      ctx.out:putf('</linearGradient><mask id="m%u" maskUnits="userSpaceOnUse" x="%g" y="%g" width="%g" height="%g">\n',
        def_id, 0, 0, x + cw * args.cols + 2, ctx.h + ch * (rows + ctx_rows * 2))
      ctx.out:putf('<rect x="%g" y="%g" width="%g" height="%g" stroke="transparent" fill="url(\'#lg%u\')"/></mask></defs>\n',
        x - 1, ctx.h, cw * args.cols + 2, ch * (rows + ctx_rows * 2), def_id)
      grid_mask = ([[ mask="url('#m%u')"]]):format(def_id)
    end
    ctx.h = ctx.h + ch * ctx_rows
    local y = ctx.h
    local row_bias = args.row_bias or 0
    local s_scale = args.s_scale or 1
    for row = -ctx_rows, rows + ctx_rows - 1 do
      for col = 0, args.cols - 1 do
        -- cell inner
        local label = ""
        if row >= 0 and row < rows then
          local lr, lc = lane_transform(row, col)
          if type(lr) == "string" then
            label = lr
          elseif lr ~= nil then
            lr = lr + row_bias
            local color
            if args.transp then
              color = hsl(lr * 0.5 * 0.101125, 1 - lc * s_scale * 2 * 0.101125, 0.92)
            else
              color = hsl(lc * 0.101125, 1 - lr * s_scale * 2 * 0.101125, 0.92)
            end
            ctx.out:putf('<rect x="%g" y="%g" width="%g" height="%g" stroke="transparent" fill="%s"/>\n',
              x + col * cw, y + row * ch, cw, ch, color)
            label = (args.skip_row_label and "" or chars:sub(lr + 1, lr + 1)) .. chars:sub(lc + 1, lc + 1)
          end
        end
        if label ~= "" then
          ctx.out:putf('<text x="%d" y="%d" text-anchor="middle" dominant-baseline="middle" font-family="monospace">%s</text>\n',
            x + (col + 0.5) * cw, y + (row + 0.5) * ch, label)
        end
      end
    end
    for row = -ctx_rows, rows + ctx_rows do
      -- horizontal lines
      ctx.out:putf('<path d="M %g %g L %g %g" stroke="black" fill="transparent" stroke-width="1"%s/>\n',
        x, y + row * ch, x + cw * args.cols, y + row * ch, grid_mask)
    end
    for col = 0, args.cols do
      -- vertical lines
      ctx.out:putf('<path d="M %g %g L %g %g" stroke="black" fill="transparent" stroke-width="1"%s/>\n',
        x + col * cw, y - ctx_rows * ch, x + col * cw, y + (rows + ctx_rows) * ch, grid_mask)
    end
    local label = args.label
    if type(label) ~= "table" then
      label = {label}
    end
    local line_spacing = ch
    local y = ctx.h + ch * rows * 0.5 - line_spacing * 0.5 * (#label - 1)
    for _, line in ipairs(label) do
      ctx.out:putf('<text x="%d" y="%d" text-anchor="end" dominant-baseline="middle" font-family="monospace">%s</text>\n',
        ctx.margin - 5, y, line)
      y = y + line_spacing
    end
    ctx.h = ctx.h + ch * (rows + ctx_rows)
  end,
  LReg = function(ctx, args)
    args.cols = 8
    ctx:Grid(args)
  end,
  To = function(ctx, args)
    local ypad = 10
    local height = 40
    local x = ctx.margin + math.floor((ctx.w - ctx.margin) / 16) * 8
    Drawing.ThickArrow(ctx.out, x, ctx.h + ypad, "->", x, ctx.h + ypad + height, "black", 3)
    ctx.h = ctx.h + ypad + height + ypad
  end,
  ToFrom = function(ctx, args)
    local xpad = 40
    local ypad = 10
    local height = 40
    local x = ctx.margin + math.floor((ctx.w - ctx.margin) / 16) * 8
    Drawing.ThickArrow(ctx.out, x - xpad, ctx.h + ypad, "->", x - xpad, ctx.h + ypad + height, "black", 3)
    Drawing.ThickArrow(ctx.out, x + xpad, ctx.h + ypad, "<-", x + xpad, ctx.h + ypad + height, "black", 3)
    ctx.out:putf('<text x="%d" y="%d" text-anchor="middle" dominant-baseline="middle" font-size="125%%">or</text>\n',
      x, ctx.h + ypad + height * 0.5)
    ctx.h = ctx.h + ypad + height + ypad
  end,
  Dst = function(ctx, args)
    if not args.label then args.label = "Dst" end
    args.cols = 16
    args.ctx_rows = 1
    ctx:Grid(args)
  end,
  SrcRow = function(ctx, args)
    args.cols = 16
    args.rows = 1
    args.ctx_rows = 1
    ctx:Grid(args)
  end,
  Src16Rows = function(ctx, args)
    args.cols = 16
    args.rows = 16
    args.ctx_rows = 1
    args.s_scale = 0.25
    local label = {}
    for i = 1, 16 do
      label[i] = args.label(i-1)
    end
    args.label = label
    ctx:Grid(args)
  end,
  Or = function(ctx, args)
    local x = ctx.margin + math.floor((ctx.w - ctx.margin) / 16) * 8
    local h = 40
    ctx.out:putf('<text x="%d" y="%d" text-anchor="middle" dominant-baseline="middle" font-size="125%%">or</text>\n',
      x, ctx.h + h * 0.5)
    ctx.h = ctx.h + h
  end,
  Spacer = function(ctx)
    ctx.h = ctx.h + 10
  end,
}}

local diagrams = {
  SFPLOAD = function(ctx)
    ctx:Dst{lane_transform = dst_even}
    ctx:Or()
    ctx:Dst{lane_transform = dst_odd}
    ctx:To()
    ctx:LReg{label = "LReg[VD]"}
  end,
  SFPSTORE = function(ctx)
    ctx:LReg{label = "LReg[VD]"}
    ctx:To()
    ctx:Dst{lane_transform = dst_even}
    ctx:Or()
    ctx:Dst{lane_transform = dst_odd}
  end,
  SHFLROR1 = function(ctx)
    ctx:LReg{label = "LReg[VC]"}
    ctx:To()
    ctx:LReg{label = "LReg[VD]", lane_transform = function(r, c)
      return r, (c - 1) % 8
    end}
  end,
  SHFLSHR1 = function(ctx)
    ctx:LReg{label = "LReg[VC]", lane_transform = function(r, c)
      if c ~= 7 then
        return r, c
      end
    end}
    ctx:To()
    ctx:LReg{label = "LReg[VD]", lane_transform = function(r, c)
      if c == 0 then
        return "Bug"
      else
        return r, (c - 1) % 8
      end
    end}
  end,
  SHFLSHR1_BH = function(ctx)
    ctx:LReg{label = "LReg[VC]", lane_transform = function(r, c)
      if c ~= 7 then
        return r, c
      end
    end}
    ctx:To()
    ctx:LReg{label = "LReg[VD]", lane_transform = function(r, c)
      if c == 0 then
        return "0"
      else
        return r, (c - 1) % 8
      end
    end}
  end,
  COPY4 = function(ctx)
    local s_scale = 0.5
    ctx:LReg{label = "LReg[0]", transp = true, s_scale = s_scale, lane_transform = function() return "" end}
    for i = 1, 3 do
      ctx:Spacer()
      ctx:LReg{label = "LReg[".. i .."]", transp = true, s_scale = s_scale, row_bias = (i - 1) * 4}
    end
    ctx:To()
    for i = 0, 2 do
      ctx:LReg{label = "LReg[".. i .."]", transp = true, s_scale = s_scale, row_bias = i * 4}
      ctx:Spacer()
    end
    ctx:LReg{label = "LReg[3]", transp = true, s_scale = s_scale, lane_transform = function() return "0" end}
  end,
  CHAINED_COPY4 = function(ctx)
    local s_scale = 0.5
    ctx:LReg{label = "LReg[0]", transp = true, s_scale = s_scale, lane_transform = function(r, c) if r > 0 then return r, c end end}
    for i = 1, 3 do
      ctx:Spacer()
      ctx:LReg{label = "LReg[".. i .."]", transp = true, s_scale = s_scale, row_bias = i * 4}
    end
    ctx:To()
    for i = 0, 2 do
      ctx:LReg{label = "LReg[".. i .. "]", transp = true, s_scale = s_scale, row_bias = (i + 1) * 4}
      ctx:Spacer()
    end
    ctx:LReg{label = "LReg[3]", transp = true, s_scale = s_scale, lane_transform = function(r, c) if r < 3 then return r+1, c else return "0" end end}
  end,
  SHFLROR1_AND_COPY4 = function(ctx)
    local s_scale = 0.5
    ctx:LReg{label = "LReg[VC]", s_scale = s_scale, transp = true}
    ctx:Spacer()
    ctx:LReg{label = "LReg[0]", s_scale = s_scale, transp = true, lane_transform = function() return "" end}
    for i = 1, 3 do
      ctx:Spacer()
      ctx:LReg{label = "LReg[".. i .."]", s_scale = s_scale, transp = true, row_bias = i * 4}
    end
    ctx:To()
    for i = 0, 2 do
      ctx:LReg{label = "LReg[".. i .."]", s_scale = s_scale, transp = true, row_bias = (i + 1) * 4}
      ctx:Spacer()
    end
    ctx:LReg{label = "LReg[3]", s_scale = s_scale, transp = true, lane_transform = function(r, c)
      return r, (c - 1) % 8
    end}
  end,
  SFPTRANSP = function(ctx)
    local s_scale = 0.5
    for g = 0, 4, 4 do
      local chars0
      if g ~= 0 then
        ctx.h = 5.5
        ctx.out:putf('<g transform="translate(%g 0)">\n', ctx.w + 5.5)
        chars0 = chars
        chars = chars0:lower()
      end
      for i = 0, 3 do
        if i ~= 0 then ctx:Spacer() end
        ctx:LReg{label = ("LReg[%u]"):format(g + i), s_scale = s_scale, transp = true, row_bias = i * 4}
      end
      ctx:ToFrom()
      for i = 0, 3 do
        if i ~= 0 then ctx:Spacer() end
        ctx:LReg{label = ("LReg[%u]"):format(g + i), s_scale = s_scale, transp = true, lane_transform = function(r, c) return r * 4 + i, c end}
      end
      if g ~= 0 then
        ctx.out:putf('</g>\n', ctx.w)
        ctx.w_scale = 2
        chars = chars0
      end
    end
  end,
  SFPCONFIG = function(ctx)
    ctx:LReg{label = "LReg[0]", skip_row_label = true, lane_transform = function(r, c) if r == 0 then return r, c end end}
    ctx:To()
    ctx:LReg{label = "LReg[VD]", skip_row_label = true, lane_transform = function(r, c) return 0, c end}
  end,
  SHIFTXB0 = function(ctx)
    ctx:SrcRow{label = "SrcB[i]", skip_row_label = true}
    ctx:To()
    ctx:SrcRow{label = "SrcB[i]", skip_row_label = true, lane_transform = function(r, c)
      return r, (c + 1) % 16
    end}
  end,
  SHIFTXB1 = function(ctx)
    ctx:SrcRow{label = "SrcB[i]", skip_row_label = true, lane_transform = function(r, c)
      if c ~= 0 then
        return r, c
      end
    end}
    ctx:To()
    ctx:SrcRow{label = "SrcB[i]", skip_row_label = true, lane_transform = function(r, c)
      if c == 15 then
        return "0"
      else
        return r, c + 1
      end
    end}
  end,
  SHIFTXA2 = function(ctx)
    ctx:Src16Rows{label = function(r) return "SrcA[?]" end, lane_transform = function(r, c)
      if c ~= 15 then
        return r, c
      end
    end}
    ctx:To()
    ctx:Src16Rows{label = function(r) return ("SrcA[%2d]"):format(r) end, lane_transform = function(r, c)
      if c == 0 then
        return "0"
      else
        return r, c - 1
      end
    end}
  end,
  SHIFTXA3 = function(ctx)
    ctx:Src16Rows{label = function(r) return "SrcA[?]" end, lane_transform = function(r, c)
      if c ~= 0 then
        return r, c
      end
    end}
    ctx:To()
    ctx:Src16Rows{label = function(r) return ("SrcA[%2d]"):format(r) end, lane_transform = function(r, c)
      if c == 15 then
        return "0"
      else
        return r, c + 1
      end
    end}
  end,
  TRNSPSRCB = function(ctx)
    ctx:Src16Rows{label = function(r) return ("SrcB[%2d]"):format(16+r) end}
    ctx:ToFrom()
    ctx:Src16Rows{label = function(r) return ("SrcB[%2d]"):format(16+r) end, lane_transform = function(r, c) return c, r end}
  end,
  MVMUL = function(ctx)
    ctx:SrcRow{label = "SrcB[i]", skip_row_label = true}
    ctx:To()
    ctx:Grid{label = "7x16", rows = 7, cols = 16, skip_row_label = true, lane_transform = function(r, c)
      if r % 2 == 0 then
        return 0, c
      else
        return "0"
      end
    end}
  end,
}
local function do_diagram(which)
  local fn = diagrams[which] or error("Unknown diagram ".. which)
  local ctx = setmetatable({out = buffer.new(), h = 5.5, w = 75.5 + 16 * 30, margin = 75.5}, ctx_mt)
  fn(ctx)
  ctx.h = ctx.h + 5.5
  ctx.w = (ctx.w + 5.5) * (ctx.w_scale or 1)
  local out = buffer.new()
  out:putf([[<svg version="1.1" width="%u" height="%u" xmlns="http://www.w3.org/2000/svg">]], ctx.w, ctx.h)
  out:putf([[<rect width="%u" height="%u" rx="5" stroke="transparent" fill="white"/>]], ctx.w, ctx.h)
  out:put(ctx.out)
  out:putf"</svg>"
  assert(io.open(own_dir .."../Out/CrossLane_".. which ..".svg", "w")):write(out)
end

local which = ...
if which == nil then
  for k in pairs(diagrams) do
    do_diagram(k)
  end
elseif which then
  do_diagram(which)
end
