local M = {}
M.instances = {}

function M.create_buffer_prefix_name()
	local tbl = {}
	local alphabet = "abcdefghijklmnopqrstuvwxyz0123456789"
	for i = 1, 20 do
		local pos = math.random(1, string.len(alphabet))
		table.insert(tbl, string.sub(alphabet, pos, pos))
	end
	return table.concat(tbl, "")
end

return M