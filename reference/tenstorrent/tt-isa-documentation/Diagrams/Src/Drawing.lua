-- SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
--
-- SPDX-License-Identifier: Apache-2.0

local type, ipairs, error, setmetatable = type, ipairs, error, setmetatable
module"Drawing"
local mux_color = "#e9ddaf"

local dims_mt = {
  __index = dims_methods,
}
local dims_methods = {
}

function _FixupRectDims(dims)
  if not dims.x then
    if dims.right then
      dims.x = dims.right - dims.w
    elseif dims.x_middle then
      dims.x = dims.x_middle - dims.w * 0.5
    end
  end
  if not dims.w and dims.right then
    dims.w = dims.right - dims.x
  end
  if not dims.y then
    if dims.bottom then
      dims.y = dims.bottom - dims.h
    elseif dims.y_middle then
      dims.y = dims.y_middle - dims.h * 0.5
    end
  end
  if not dims.h and dims.bottom then
    dims.h = dims.bottom - dims.y
  end
  dims.right = dims.x + dims.w
  dims.bottom = dims.y + dims.h
  dims.x_middle = dims.x + dims.w * 0.5
  dims.y_middle = dims.y + dims.h * 0.5
  setmetatable(dims, dims_mt)
end

function RectText(out, dims, text)
  _FixupRectDims(dims)
  if dims.rhombus then
    out:putf([[<path d="M %g %g L %g %g L %g %g L %g %g Z" stroke="black" fill="%s" stroke-width="1"/>]],
      dims.x, dims.y_middle,
      dims.x_middle, dims.y,
      dims.right, dims.y_middle,
      dims.x_middle, dims.bottom,
      dims.color or "white")
  else
    out:putf([[<rect x="%g" y="%g" width="%g" height="%g" stroke="black" fill="%s" stroke-width="1"/>]],
      dims.x, dims.y, dims.w, dims.h, dims.color or "white")
  end
  if type(text) == "string" then
    text = {text}
  end
  local line_spacing = 20
  local y = dims.y + (dims.h_text or dims.h) * 0.5 - line_spacing * 0.5 * (#text - 1)
  for _, line in ipairs(text) do
    out:putf([[<text x="%d" y="%d" text-anchor="middle" dominant-baseline="middle">%s</text>]],
      dims.x + dims.w * 0.5, y, line)
    y = y + line_spacing
  end
  return dims
end

function Mux(out, dims, direction)
  _FixupRectDims(dims) -- Rect dimensions are of bounding box
  -- [1] - [2]
  --  |     |
  -- [4] - [3]
  local x = {dims.x, dims.x + dims.w, dims.x + dims.w, dims.x}
  local y = {dims.y, dims.y, dims.y + dims.h, dims.y + dims.h}
  local slope = 0.5
  if direction == "<" then
    -- [1] moves down, [4] moves up
    local dy = dims.w * slope
    y[1] = y[1] + dy
    y[4] = y[4] - dy
  elseif direction == ">" then
    -- [2] moves down, [3] moves up
    local dy = dims.w * slope
    y[2] = y[2] + dy
    y[3] = y[3] - dy
  elseif direction == "^" then
    -- [1] moves right, [2] moves left
    local dx = dims.h * slope
    x[1] = x[1] + dx
    x[2] = x[2] - dx
  elseif direction == "v" then
    -- [4] moves right, [3] moves left
    local dx = dims.h * slope
    x[4] = x[4] + dx
    x[3] = x[3] - dx
  else
    error("Unknown direction ".. direction)
  end
  out:putf([[<path d="M %g %g L %g %g L %g %g L %g %g Z" stroke="black" fill="%s" stroke-width="1"/>]],
      x[1], y[1],
      x[2], y[2],
      x[3], y[3],
      x[4], y[4],
      mux_color)
  dims.mux = true
  return dims
end

function LineTextAbove(out, from, to, text)
  if type(text) ~= "table" then
    text = {text}
  end
  out:putf([[<path d="M %g %g L %g %g" stroke="black" fill="transparent" stroke-width="1"/>]],
    from[1], from[2],
    to[1], to[2])
  if text[1] then
    local x = (from[1] + to[1]) * 0.5
    local y = (from[2] + to[2]) * 0.5
    out:putf([[<text x="%d" y="%d" text-anchor="middle">%s</text>]],
      x, y - 4, text[1])
  end
end

function ThickArrow(out, x1, y1, heads, x2, y2, color, size)
  -- dx/dy is unit vector in direction x1/y1 -> x2/y2
  local dx = x2 - x1
  local dy = y2 - y1
  local scale = (dx * dx + dy * dy) ^ -0.5
  dx = dx * scale
  dy = dy * scale

  -- nx/ny is normal to dx/dy
  local nx, ny = dy, -dx

  local xs = {}
  local ys = {}
  local function append(px, py)
    xs[#xs + 1] = px
    ys[#ys + 1] = py
  end

  size = size or 1
  local hw = 2 * size -- half-width for stem
  local aw = 6 * size -- half-width for heads

  if heads:sub(1, 1) == "<" then
    append(x1 + dx * aw + nx * hw, y1 + dy * aw + ny * hw)
    append(x1 + dx * aw + nx * aw, y1 + dy * aw + ny * aw)
    append(x1, y1)
    append(x1 + dx * aw - nx * aw, y1 + dy * aw - ny * aw)
    append(x1 + dx * aw - nx * hw, y1 + dy * aw - ny * hw)
  elseif heads:sub(1, 1) == ">" then
    append(x1 + nx * hw, y1 + ny * hw)
    append(x1 + dx * hw * 0.5, y1 + dy * hw * 0.5)
    append(x1 - nx * hw, y1 - ny * hw)
  else
    append(x1 + nx * hw, y1 + ny * hw)
    append(x1 - nx * hw, y1 - ny * hw)
  end

  if heads:sub(2, 2) == ">" then
    append(x2 - dx * aw - nx * hw, y2 - dy * aw - ny * hw)
    append(x2 - dx * aw - nx * aw, y2 - dy * aw - ny * aw)
    append(x2, y2)
    append(x2 - dx * aw + nx * aw, y2 - dy * aw + ny * aw)
    append(x2 - dx * aw + nx * hw, y2 - dy * aw + ny * hw)
  elseif heads:sub(2, 2) == "<" then
    append(x2 - nx * hw, y2 - ny * hw)
    append(x2 - dx * hw * 0.5, y2 - dy * hw * 0.5)
    append(x2 + nx * hw, y2 + ny * hw)
  else
    append(x2 - nx * hw, y2 - ny * hw)
    append(x2 + nx * hw, y2 + ny * hw)
  end

  out:putf([[<path d="]])
  local cmd = "M"
  for i, x in ipairs(xs) do
    local y = ys[i]
    out:putf("%s %g %g", cmd, x, y)
    cmd = " L"
  end
  out:putf([[ Z" stroke="transparent" fill="%s" stroke-width="1"/>]], color):put("\n")
end

function MultiLine(out, data)
  if data.thick then data.stroke_width = data.thick * 4 end
  local x = data[1]
  local y = data[2]
  out:putf([[<path d="M %g %g]], x, y)
  local xs = {x}
  local ys = {y}
  for i = 3, #data, 2 do
    local cmd = data[i]
    local val = data[i + 1]
    if cmd == ">" or cmd == "<" then
      x = val
    elseif cmd == "^" or cmd == "v" then
      y = val
    else
      error("Invalid command ".. cmd)
    end
    xs[#xs + 1] = x
    ys[#ys + 1] = y
  end
  local function in_dir_of(x, y, ox, oy)
    local dx = ox - x
    local dy = oy - y
    local scale = (dx * dx + dy * dy) ^ -0.5 * 3
    dx = dx * scale
    dy = dy * scale
    return x + dx, y + dy
  end
  for i = 2, #xs - 1 do
    local x0, x1, x2 = xs[i - 1], xs[i], xs[i + 1]
    local y0, y1, y2 = ys[i - 1], ys[i], ys[i + 1]
    local cx, cy = in_dir_of(x1, y1, x0, y0)
    out:putf(" L %g %g", cx, cy)
    out:putf(" Q %g %g %g %g", x1, y1, in_dir_of(x1, y1, x2, y2))
  end
  x, y = xs[#xs], ys[#ys]
  local x0, y0
  if data.thick then
    local dx, dy = in_dir_of(0, 0, xs[#xs - 1] - x, ys[#ys - 1] - y)
    x0 = x + dx * data.stroke_width / 3
    y0 = y + dy * data.stroke_width / 3
  end
  local color = data.color or "black"
  out:putf([[ L %g %g" stroke="%s" fill="transparent" stroke-width="%g"/>]], x0 or x, y0 or y, color, data.stroke_width or 1)
  if data.thick then
    ThickArrow(out, x0, y0, "->", x, y, color, data.thick)
  elseif data.head ~= false then
    local dx, dy = in_dir_of(0, 0, xs[#xs - 1] - x, ys[#ys - 1] - y)
    local nx, ny = dy, -dx
    out:putf([[<path d="M %g %g L %g %g L %g %g" stroke="%s" fill="transparent" stroke-width="1"/>]],
      x + dx + nx, y + dy + ny,
      x, y,
      x + dx - nx, y + dy - ny,
      color)
  end
  if data.head == "both" then
    x, y = xs[1], ys[1]
    local dx, dy = in_dir_of(0, 0, xs[2] - x, ys[2] - y)
    local nx, ny = dy, -dx
    out:putf([[<path d="M %g %g L %g %g L %g %g" stroke="%s" fill="transparent" stroke-width="1"/>]],
      x + dx + nx, y + dy + ny,
      x, y,
      x + dx - nx, y + dy - ny,
      color)
  end
end

function MultiLineWithGaps(out, data)
  local x = data[1]
  local y = data[2]
  local sub_data = {x, y, color = data.color}
  for i = 3, #data, 2 do
    local cmd = data[i]
    local val = data[i + 1]
    if cmd:sub(1, 1) == " " then
      sub_data.head = false
      MultiLine(out, sub_data)
      sub_data = {color = data.color}
      cmd = cmd:sub(2, 2)
    end
    if cmd == ">" or cmd == "<" then
      x = val
    elseif cmd == "^" or cmd == "v" then
      y = val
    else
      error("Invalid command ".. cmd)
    end
    if #sub_data == 0 then
      sub_data[1] = x
      sub_data[2] = y
    else
      sub_data[#sub_data + 1] = cmd
      sub_data[#sub_data + 1] = val
    end
  end
  MultiLine(out, sub_data)
end