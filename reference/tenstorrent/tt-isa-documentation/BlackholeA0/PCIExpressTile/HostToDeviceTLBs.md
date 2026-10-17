# PCI Express Tile Configurable Host-To-Device TLBs

There are 210 configurable TLB windows, which collectively occupy the low 404 MiB of BAR 0 and all 32 GiB of BAR 4. Each window can be pointed at an aligned region of the address space of any tile in the NoC.

|TLB Index|Size|Location|Notes|
|--:|---|---|---|
|0<br/>through&nbsp;31|2 MiB each<br/>64 MiB total|BAR 0|If used for multicast, these TLBs support non-rectangular multicast|
|32<br/>through&nbsp;200|2 MiB each<br/>338 MiB total|BAR 0||
|201|2 MiB|BAR 0|Reserved for use by the kernel driver|
|202<br/>through&nbsp;209|4 GiB each<br/>32 GiB total|BAR 4|Can be used to expose all of the on-device GDDR6|

## Configuration

Most TLB windows have 96 bits of configuration associated with each of them, and then the first 32 windows have an additional 32 bits each for configuring non-rectangular multicast. Recent versions of [tt-kmd](https://github.com/tenstorrent/tt-kmd/) provide `TENSTORRENT_IOCTL_ALLOCATE_TLB` / `TENSTORRENT_IOCTL_CONFIGURE_TLB` / `TENSTORRENT_IOCTL_FREE_TLB` ioctls for software wishing to delegate the details to the kernel driver. If software does its own TLB management, the relevant configuration exists in BAR0 starting at address `0x1FC0_0000`:
```c
struct { uint32_t low32, mid32, high32; } windows[210];
uint32_t strided[32];
```

The `low32`, `mid32`, and `high32` fields are concatenated to form 96 bits, the meaning of which is:

<table><thead><tr><th colspan="2">2 MiB TLB</th><th colspan="2">4 GiB TLB</th><th colspan="2"></th></tr><tr><th>First&nbsp;bit</th><th>#&nbsp;Bits</th><th>First&nbsp;bit</th><th>#&nbsp;Bits</th><th>Name</th><th>Purpose</th></tr></thead>
<tr><td align="right">0</td><td align="right">43</td><td align="right">0</td><td align="right">32</td><td><code>local_offset</code></td><td>When a TLB is accessed, hardware needs to form a 64-bit address within the target tile(s). The high bits of that address come from this <code>local_offset</code>, and the low bits come from the accessed offset within the TLB.</td></tr>
<tr><td align="right">43</td><td align="right">6</td><td align="right">32</td><td align="right">6</td><td><code>x_end</code></td><td>When <code>mcast</code> is set, the <a href="../NoC/Coordinates.md">X coordinate</a> of the end of the multicast rectangle. Otherwise the X coordinate of the single target tile.</td></tr>
<tr><td align="right">49</td><td align="right">6</td><td align="right">38</td><td align="right">6</td><td><code>y_end</code></td><td>When <code>mcast</code> is set, the <a href="../NoC/Coordinates.md">Y coordinate</a> of the end of the multicast rectangle. Otherwise the Y coordinate of the single target tile.</td></tr>
<tr><td align="right">55</td><td align="right">6</td><td align="right">44</td><td align="right">6</td><td><code>x_start</code></td><td>When <code>mcast</code> is set, the <a href="../NoC/Coordinates.md">X coordinate</a> of the start of the multicast rectangle. Ignored otherwise.</td></tr>
<tr><td align="right">61</td><td align="right">6</td><td align="right">50</td><td align="right">6</td><td><code>y_start</code></td><td>When <code>mcast</code> is set, the <a href="../NoC/Coordinates.md">Y coordinate</a> of the start of the multicast rectangle. Ignored otherwise.</td></tr>
<tr><td align="right">67</td><td align="right">1</td><td align="right">56</td><td align="right">1</td><td><code>noc_sel</code></td><td><code>0</code> to select NoC #0, <code>1</code> to select NoC #1.</td></tr>
<tr><td align="right">68</td><td align="right">1</td><td align="right">57</td><td align="right">1</td><td>Reserved</td><td>Software should write as zero.</td></tr>
<tr><td align="right">69</td><td align="right">1</td><td align="right">58</td><td align="right">1</td><td><code>mcast</code></td><td>Equivalent to <code>NOC_CMD_BRCST_PACKET</code>; <code>true</code> causes <code>(x_start, y_start)</code> through <code>(x_end, y_end)</code> to specify a rectangle of target Tensix tiles, whereas <code>false</code> causes <code>(x_end, y_end)</code> to specify a single tile target.</td></tr>
<tr><td align="right">70</td><td align="right">2</td><td align="right">59</td><td align="right">2</td><td><code>ordering</code></td><td>Four possible ordering modes:<ul><li><code>0</code> - Default</li><li><code>1</code> - Strict AXI</li><li><code>2</code> - Posted Writes</li><li><code>3</code> - Counted Writes</li></ul></td></tr>
<tr><td align="right">72</td><td align="right">1</td><td align="right">61</td><td align="right">1</td><td><code>linked</code></td><td>Similar to <a href="../NoC/MemoryMap.md#noc_ctrl"><code>NOC_CMD_VC_LINKED</code></a>. It is never safe to set this to <code>true</code>, as the kernel driver reserves the right to use its TLB window at any time, and <em>it</em> always has <code>linked</code> set to <code>false</code>.</td></tr>
<tr><td align="right">73</td><td align="right">1</td><td align="right">62</td><td align="right">1</td><td><code>static_vc</code></td><td>Equivalent to <a href="../NoC/MemoryMap.md#noc_ctrl"><code>NOC_CMD_VC_STATIC</code></a></td></tr>
<tr><td align="right">74</td><td align="right">1</td><td align="right">63</td><td align="right">1</td><td>Reserved</td><td>Software should write as zero.</td></tr>
<tr><td align="right">75</td><td align="right">1</td><td align="right">64</td><td align="right">1</td><td><code>static_vc_buddy</code></td><td>When <code>static_vc</code> is set, the buddy bit. Ignored otherwise.</td></tr>
<tr><td align="right">76</td><td align="right">2</td><td align="right">65</td><td align="right">2</td><td><code>static_vc_class</code></td><td>When <code>static_vc</code> is set, the class bits (which must be either <code>0b00</code> or <code>0b01</code> for unicast requests, and must be <code>0b10</code> for multicast requests). Ignored otherwise.</td></tr>
<tr><td align="right">78</td><td align="right">18</td><td align="right">67</td><td align="right">29</td><td>Reserved</td><td>Software should write as zero.</td></tr>
</table>

The first 32 TLBs have additional configuration in `strided`, which is relevant when `mcast` is `true`:

|First&nbsp;bit|#&nbsp;Bits|Name|Purpose|
|--:|--:|---|---|
|0|2|`x_keep`|If both `x_keep` and `x_skip` are non-zero, the X axis of the multicast rectangle will be masked: starting at `x_start`, `x_keep` tiles will receive the multicast, then `x_skip` tiles will be skipped, then `x_keep` tiles will receive the multicast, then `x_skip` tiles will be skipped, and so forth. If `x_keep + x_skip` is not a power of two, then `x_start` needs to be less than or equal to `x_end`. Note that the keep/skip pattern is applied to raw NoC #0 or NoC #1 coordinates.|
|2|2|`x_skip`|See `x_keep`.|
|4|2|`y_keep`|If both `y_keep` and `y_skip` are non-zero, the Y axis of the multicast rectangle will be masked: starting at `y_start`, `y_keep` tiles will receive the multicast, then `y_skip` tiles will be skipped, then `y_keep` tiles will receive the multicast, then `y_skip` tiles will be skipped, and so forth. If `y_keep + y_skip` is not a power of two, then `y_start` needs to be less than or equal to `y_end`. Note that the keep/skip pattern is applied to raw NoC #0 or NoC #1 coordinates.|
|6|2|`y_skip`|See `y_keep`.|
|8|5|`x_exclude_coord`|When `apply_exclusion` is `true`, an [X coordinate](../NoC/Coordinates.md). Note that coordinate translation is not applied to this; it is a raw NoC #0 or NoC #1 coordinate. Interpretation depends on `x_exclude_direction`.|
|13|4|`y_exclude_coord`|When `apply_exclusion` is `true`, a [Y coordinate](../NoC/Coordinates.md). Note that coordinate translation is not applied to this; it is a raw NoC #0 or NoC #1 coordinate. Interpretation depends on `y_exclude_direction`.|
|17|1|`x_exclude_direction`|When `apply_exclusion` is `true`, `x_exclude_direction` being `true` means that `x >= x_exclude_coord` are excluded, whereas `x_exclude_direction` being `false` means that `x <= x_exclude_coord` are excluded (the exact formulation is more complex when `x_start > x_end`).|
|18|1|`y_exclude_direction`|When `apply_exclusion` is `true`, `y_exclude_direction` being `true` means that `y >= y_exclude_coord` are excluded, whereas `y_exclude_direction` being `false` means that `y <= y_exclude_coord` are excluded (the exact formulation is more complex when `y_start > y_end`).|
|19|1|`apply_exclusion`|Whether to exclude a quadrant from the multicast rectangle.|
|20|1|`optimize_routing_for_exclusion`||
|21|8|`num_destinations_override`|Software must set this to the number of tiles which will receive the multicast. If there is no skipping and `apply_exclusion` is `false`, software can set this to zero, in which case hardware will substitute the correct value.|
|29|3|Reserved|Software can write to these bits, and read the value back, but they have no effect on the TLB.|
