-- Transient EFFECT templates, never item definitions or saved gear.
local function resource(guid, kind)
    return assert(game.assets.get_resource(guid, kind, 0), kind .. " missing: " .. guid)
end
local function component(data, kind)
    for _, c in ipairs(data.components) do if c.type == kind then return c.value end end
    error("Native effect component missing: " .. kind)
end
local function binary(kind, data)
    local b = buffer.create(); b:write_resource(kind, data); return b
end
local native = resource("7100f7c2-480f-44da-a354-91c08786011b", "keen::ecs::TemplateResource")
local before = binary("keen::ecs::TemplateResource", native.data):to_string()
local impact = component(native.data, "keen::ecs::Impact")
local program = resource(impact.program, "keen::ImpactProgram").data
local sprint_config, sprint_type
for _, entry in ipairs(program.dataLayout) do
    if entry.configId.value == 345127019 then sprint_config = entry.configId.value; sprint_type = entry.type.value end
end
assert(sprint_config, "Native equipment effect no longer accepts Sprint")
local index
for _, container in ipairs(game.assets.get_resources_by_type("keen::AttributeContainerResource")) do
    for _, group in ipairs(container.data.attributes) do
        for i, name in ipairs(group.debugNames) do
            if name == "Movement_Sprint" then assert(not index, "Ambiguous sprint attribute"); index = i end
        end
    end
end
assert(index, "Sprint attribute missing")
local player = resource("c3db66d6-89a5-4a6c-a8e3-d9a1f6009812", "keen::ecs::TemplateResource")
local base = component(player.data, "keen::ecs::Movement").dataStorage[index]
assert(type(base) == "number" and base > 0 and base < 40, "Invalid base sprint")
for percent = 1, 100 do
    local guid = string.format("b0030000-0300-5300-a300-00000000%04x", percent)
    local existing = game.assets.get_resource(guid, "keen::ecs::TemplateResource", 0)
    local name = string.format("SpeedMod_ServerSprint_%03d", percent)
    assert(not existing or existing.data.name == name, "Effect GUID collision")
    local effect = existing or game.assets.create_resource(native.data, "keen::ecs::TemplateResource", guid, 0)
    effect.data = native.data
    effect.data.name = name
    local config = component(effect.data, "keen::ecs::ImpactConfiguration")
    config.configValues.simple = {{
        type = "keen::impact::FloatImpactConfig",
        value = {
            locaTag = "00000000-0000-0000-0000-000000000000", isSigned = true, valueFormat = "Normal",
            id = "00000000-0000-0000-0000-000000000000", configId = { value = sprint_config }, type = { value = sprint_type },
            configGuid = "00000000-0000-0000-0000-000000000000", objectId = "00000000-0000-0000-0000-000000000000",
            value = base * percent / 100,
        },
    }}
    config.configValues.scaled = {}
    local life
    for _, c in ipairs(effect.data.components) do if c.type == "keen::ecs::LifeTime" then life = c.value end end
    if not life then
        table.insert(effect.data.components, { type = "keen::ecs::LifeTime", value = {
            lifeTime = { value = 1000000000 }, endOfLife = { value = 0 },
            enforceDestroy = true, preventDestroy = false, triggeredDestroy = false,
        }})
    else
        life.lifeTime.value = 1000000000; life.endOfLife.value = 0
        life.enforceDestroy = true; life.preventDestroy = false; life.triggeredDestroy = false
    end
    local decoded = binary("keen::ecs::TemplateResource", effect.data):read_resource("keen::ecs::TemplateResource")
    local value = component(decoded, "keen::ecs::ImpactConfiguration").configValues.simple[1].value.value
    assert(math.abs(value - base * percent / 100) < 0.00001, "Effect round-trip failed")
end
assert(binary("keen::ecs::TemplateResource", native.data):to_string() == before, "Native template changed")
print("[SpeedModServer] Prepared 100 temporary effects; no item definitions or resource heap patch.")
