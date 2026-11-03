-- Variant GUIDs are a permanent save format; NEVER change them after release.
local sprint_id, sprint_loca = 345127019, 3180863901
local function resource(guid, kind)
    return assert(game.assets.get_resource(guid, kind, 0), kind .. " missing: " .. tostring(guid))
end
local function binary(kind, data)
    local b = buffer.create(); b:write_resource(kind, data); return b
end
local donor = resource("e8ea3195-02d8-4a8e-bbd5-4a578f2d67db", "keen::ItemInfo").data
local config_donor, ui_donor
for _, c in ipairs(donor.impactValues.simple) do
    if c.value.configId.value == sprint_id then config_donor = c end
end
for _, u in ipairs(donor.uiValues) do
    if u.locaId.value == sprint_loca then ui_donor = u end
end
assert(config_donor and ui_donor, "Native sprint config/tooltip missing")
local base_speed, index = 8, nil
local player = resource("c3db66d6-89a5-4a6c-a8e3-d9a1f6009812", "keen::ecs::TemplateResource").data
for _, r in ipairs(game.assets.get_resources_by_type("keen::AttributeContainerResource")) do
    for _, group in ipairs(r.data.attributes) do
        for i, name in ipairs(group.debugNames) do if name == "Movement_Sprint" then index = i end end
    end
end
assert(index, "Movement_Sprint missing")
local validated = false
for _, c in ipairs(player.components) do
    if c.type == "keen::ecs::Movement" then
        assert(c.value.dataStorage[index] == base_speed, "Sprint base changed"); validated = true
    end
end
assert(validated, "Player Movement missing")
local registry = game.assets.get_resources_by_type("keen::ItemRegistryResource")[1].data
local registered, ids, originals, plan = {}, {}, {}, {}
for i, guid in ipairs(registry.itemRefs) do registered[tostring(guid)] = tostring(registry.dbgNames[i]) end
local function variant_name(base_id, bonus) return string.format("SM02_Boot_%08x_P%03d", base_id, bonus) end
local all_items = game.assets.get_resources_by_type("keen::ItemInfo")
for _, r in ipairs(all_items) do ids[game.guid.hash(r.guid)] = tostring(r.guid) end
-- Read GUID metadata for collision checks. Reading every ItemInfo.data causes EML to
-- serialize thousands of untouched items, which wastes the fixed resource arena.
-- All registered gameplay boots in this build have "boot" in the registry name.
-- Preflight all IDs/icons before mutation: a hash collision must never alias an item.
for _, r in ipairs(all_items) do
    local registry_name = registered[tostring(r.guid)]
    if registry_name and registry_name:lower():find("boot", 1, true) then
        local d, vanity = r.data, false
        for _, flag in ipairs(d.flags) do if flag == "IsVanityItem" then vanity = true end end
        if d.equipment.slot == "Armour_Feet" and registered[tostring(d.objectId)] and
           not vanity and not d.debugName:match("^SM02_") then
            local icon = resource(d.objectId, "keen::UiTextureResource")
            for _, c in ipairs(d.impactValues.scaled) do
                assert(c.value.configId.value ~= sprint_id, "Scaled Sprint unsupported: " .. d.debugName)
            end
            originals[#originals + 1] = { resource = r, before = binary("keen::ItemInfo", d):to_string() }
            for bonus = 0, 100 do
                local guid = string.format("%08x-%04x-5d02-a200-%s", d.itemId.value, bonus, tostring(d.objectId):sub(-12))
                local id = game.guid.hash(guid)
                assert(not ids[id] or ids[id] == guid, "ItemId collision: " .. guid)
                ids[id] = guid
                local existing = game.assets.get_resource(guid, "keen::ItemInfo", 0)
                assert(not existing or existing.data.debugName == variant_name(d.itemId.value, bonus), "Variant GUID collision")
                plan[#plan + 1] = { source = r, icon = icon, guid = guid, id = id, bonus = bonus }
            end
        end
    end
end
local csv = { "base_id,bonus_percent,variant_id,variant_guid" }
for plan_index, p in ipairs(plan) do
    local src = p.source.data
    local r = game.assets.get_resource(p.guid, "keen::ItemInfo", 0)
    if r then r.data = src else r = game.assets.create_resource(src, "keen::ItemInfo", p.guid, 0) end
    local d = r.data
    d.itemId = { value = p.id }; d.objectId = src.objectId -- native UI texture lookup reuses the base icon
    d.debugName = variant_name(src.itemId.value, p.bonus)
    local sprint
    for _, c in ipairs(d.impactValues.simple) do if c.value.configId.value == sprint_id then sprint = c end end
    if not sprint then
        table.insert(d.impactValues.simple, config_donor)
        sprint = d.impactValues.simple[#d.impactValues.simple]; sprint.value.value = 0
    end
    sprint.value.value = sprint.value.value + base_speed * p.bonus / 100
    local ui
    for _, u in ipairs(d.uiValues) do if u.locaId.value == sprint_loca then ui = u end end
    if not ui then table.insert(d.uiValues, ui_donor); ui = d.uiValues[#d.uiValues]; ui.value = 0 end
    ui.value = ui.value + p.bonus
    if not registered[p.guid] then
        table.insert(registry.itemRefs, p.guid); table.insert(registry.dbgNames, d.debugName); registered[p.guid] = true
    end
    local decoded = binary("keen::ItemInfo", d):read_resource("keen::ItemInfo")
    assert(decoded.itemId.value == p.id and decoded.debugName == d.debugName, "Variant round-trip failed")
    local checked_config, checked_ui = false, false
    for _, c in ipairs(decoded.impactValues.simple) do
        if c.value.configId.value == sprint_id then
            assert(math.abs(c.value.value - sprint.value.value) < 0.00001, "Sprint round-trip failed")
            checked_config = true
        end
    end
    for _, u in ipairs(decoded.uiValues) do
        if u.locaId.value == sprint_loca then
            assert(u.value == ui.value and not u.isScaled, "Tooltip round-trip failed")
            checked_ui = true
        end
    end
    assert(checked_config and checked_ui, "Missing sprint config/tooltip")
    table.insert(csv, string.format("%08x,%d,%08x,%s", src.itemId.value, p.bonus, p.id, p.guid))
    -- The EML bridge uses auxiliary Lua references for reflected field proxies.
    -- Release temporary proxies in batches when patching thousands of resources.
    if plan_index % 32 == 0 then collectgarbage("collect") end
end
for _, o in ipairs(originals) do assert(binary("keen::ItemInfo", o.resource.data):to_string() == o.before, "Vanilla item changed") end
if loader.features.export then
    io.export("speedmod/variants.csv", table.concat(csv, "\n") .. "\n")
    io.export("speedmod/validation.txt", string.format("SpeedMod 0.2.1: %d boots, %d variants. Originals unchanged.\n", #originals, #plan))
end
print(string.format("[SpeedMod] Prepared %d persistent variants from %d boots (0..100%%).", #plan, #originals))
