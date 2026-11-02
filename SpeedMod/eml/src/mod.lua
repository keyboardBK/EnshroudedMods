-- EML creates transient, native equipment effects. The EMF companion chooses
-- one from the ACTUAL equipped item's rarity, including random loot.
-- ItemInfo.rarity is deliberately never used to choose a runtime bonus.
local percentages = require("config")
local rarities = { "Common", "Uncommon", "Rare", "Epic", "Legendary" }
local template_guids = {
    "4fb629d0-15bc-5fc1-a1e6-f9bca873bf01",
    "4fb629d0-15bc-5fc1-a1e6-f9bca873bf02",
    "4fb629d0-15bc-5fc1-a1e6-f9bca873bf03",
    "4fb629d0-15bc-5fc1-a1e6-f9bca873bf04",
    "4fb629d0-15bc-5fc1-a1e6-f9bca873bf05",
}
local sprint_config_id = 345127019
local native_template_guid = "7100f7c2-480f-44da-a354-91c08786011b"
local player_guid = "c3db66d6-89a5-4a6c-a8e3-d9a1f6009812"
local donor_guid = "e8ea3195-02d8-4a8e-bbd5-4a578f2d67db"

local function fail(message)
    error("[SpeedMod] " .. message)
end

local function resource(guid, type_name)
    local found = game.assets.get_resource(guid, type_name, 0)
    if not found then fail("Required resource missing: " .. type_name .. " " .. guid) end
    return found
end

local function find_component(template, type_name)
    for _, component in ipairs(template.components) do
        if component.type == type_name then return component.value end
    end
    fail("Required component missing: " .. type_name)
end

local native = resource(native_template_guid, "keen::ecs::TemplateResource")
local function binary(type_name, data)
    local result = buffer.create()
    result:write_resource(type_name, data)
    return result:to_string()
end
local native_before = binary("keen::ecs::TemplateResource", native.data)
local native_config = find_component(native.data, "keen::ecs::ImpactConfiguration")
local impact = find_component(native.data, "keen::ecs::Impact")
local program = resource(impact.program, "keen::ImpactProgram")
local has_native_sprint = false
for _, entry in ipairs(program.data.dataLayout) do
    if entry.configId.value == sprint_config_id then has_native_sprint = true end
end
if not has_native_sprint then fail("The native equipment program no longer supports Sprint config.") end

-- Find the sprint index from the registry, rather than guessing array indices.
local sprint_index
for _, container in ipairs(game.assets.get_resources_by_type("keen::AttributeContainerResource")) do
    for _, group in ipairs(container.data.attributes) do
        for index, name in ipairs(group.debugNames) do
            if name == "Movement_Sprint" then
                if sprint_index then fail("Ambiguous Movement_Sprint declaration.") end
                sprint_index = index
            end
        end
    end
end
if not sprint_index then fail("Movement_Sprint declaration missing.") end
local movement = find_component(resource(player_guid, "keen::ecs::TemplateResource").data, "keen::ecs::Movement")
local base_speed = movement.dataStorage[sprint_index]
if type(base_speed) ~= "number" or base_speed <= 0 or base_speed > 40 then
    fail("Invalid base sprint speed; refusing to create effects.")
end

local donor = resource(donor_guid, "keen::ItemInfo")
local donor_before = binary("keen::ItemInfo", donor.data)
local sprint_variant
for _, config in ipairs(donor.data.impactValues.simple) do
    if config.value.configId.value == sprint_config_id then
        if config.type ~= "keen::impact::FloatImpactConfig" then fail("Sprint config type changed.") end
        sprint_variant = config
    end
end
if not sprint_variant then fail("Native sprint configuration donor missing.") end
for _, rarity in ipairs(rarities) do
    local value = percentages[rarity]
    if type(value) ~= "number" or value ~= value or value < 0 or value > 100 then
        fail("Bonus for " .. rarity .. " must be a finite number between 0 and 100.")
    end
end
for index, guid in ipairs(template_guids) do
    local existing = game.assets.get_resource(guid, "keen::ecs::TemplateResource", 0)
    if existing and existing.data.name ~= "SpeedMod_BootSprint_" .. rarities[index] then
        fail("Effect GUID is owned by another resource: " .. guid)
    end
end

-- All validation above happens before changing resources. Only NEW resources
-- are written: no player template, existing item, perk, loot, or save is edited.
local report = {
    "SpeedMod 0.1.0 / EML resources",
    "Game: " .. tostring(game.version),
    "Base sprint speed: " .. tostring(base_speed),
    "Actual equipped rarity selection requires the EMF runtime companion.",
    "Existing item and perk definitions remain unchanged.",
}
local csv = { "rarity,bonus_percent,additive_speed,template_guid" }
for index, rarity in ipairs(rarities) do
    local effect = game.assets.get_resource(template_guids[index], "keen::ecs::TemplateResource", 0)
    if effect then
        effect.data = native.data -- reset our own resource, so reapplication never duplicates components
    else
        effect = game.assets.create_resource(native.data, "keen::ecs::TemplateResource", template_guids[index], 0)
    end
    effect.data.name = "SpeedMod_BootSprint_" .. rarity
    local configuration = find_component(effect.data, "keen::ecs::ImpactConfiguration")
    -- Assigning a field makes a deep copy in EML. The donor stays untouched.
    configuration.configValues.simple = { sprint_variant }
    configuration.configValues.scaled = {}
    local amount = base_speed * percentages[rarity] / 100
    configuration.configValues.simple[1].value.value = amount

    -- Native shutdown code undoes the bonus. Lifetime is a watchdog if the
    -- runtime companion is disabled; its endOfLife is renewed while worn.
    table.insert(effect.data.components, {
        type = "keen::ecs::LifeTime",
        value = {
            lifeTime = { value = 1000000000 },
            endOfLife = { value = 0 },
            enforceDestroy = true,
            preventDestroy = false,
            triggeredDestroy = false,
        },
    })
    local serialized = buffer.create()
    serialized:write_resource("keen::ecs::TemplateResource", effect.data)
    local decoded = serialized:read_resource("keen::ecs::TemplateResource")
    local decoded_config = find_component(decoded, "keen::ecs::ImpactConfiguration")
    if math.abs(decoded_config.configValues.simple[1].value.value - amount) > 0.00001 then
        fail("Effect binary round-trip failed for " .. rarity)
    end
    -- Exercise the actual EML binary writer even in export-only validation.
    if loader.features.export then
        io.export("speedmod/effect_" .. rarity .. ".bin", serialized)
    end
    local line = string.format("%s: +%g%% (%g speed)", rarity, percentages[rarity], amount)
    print("[SpeedMod] " .. line)
    table.insert(report, line)
    table.insert(csv, string.format("%s,%g,%.9g,%s", rarity, percentages[rarity], amount, template_guids[index]))
end
if binary("keen::ecs::TemplateResource", native.data) ~= native_before or
   binary("keen::ItemInfo", donor.data) ~= donor_before then
    fail("Unexpected mutation of a vanilla resource.")
end
if loader.features.export then
    io.export("speedmod/effects.csv", table.concat(csv, "\n") .. "\n")
    io.export("speedmod/validation.txt", table.concat(report, "\n") .. "\n")
end
print("[SpeedMod] Prepared 5 native effects. Runtime companion required for gameplay bonuses.")
