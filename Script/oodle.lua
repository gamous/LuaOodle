oodle = require("oo2net9")
oo_state,oo_shared,oo_window=oodle.Initialize()
local function Oodle_decompress(tvb_range,uncomp_len,name)
    if oodle == nil then
      return nil
    end
    local comp_data = tvb_range:raw()
    local comp_len = tvb_range:len()
    raw=oodle.Decompress(comp_data,comp_len,uncomp_len)
    ba=ByteArray.new(raw,true)
    return ba:tvb(name):range(0,uncomp_len)
end