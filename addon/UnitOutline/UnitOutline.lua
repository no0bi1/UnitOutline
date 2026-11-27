BINDING_HEADER_UNITOUTLINE = "Unit Outline"
BINDING_NAME_UNITOUTLINE_TOGGLE = "Toggle outline"

local defaults = {
    enable = 1, auto = 0, players = 1, npcs = 1, thickness = 2, opacity = 1,
    tsrc = { 1, 1, 1 }, msrc = { 1, 1, 1 },
    hostile = { 1, 0.1, 0.1 }, neutral = { 1, 1, 0 }, friendly = { 0, 0.8, 0 },
}

local panel, enableCheck
local plates = {}
local sent -- last enable value pushed to the DLL

-- smart mode: player in combat and more than one attackable, living, in-combat unit with a nameplate
local function Engaged()
    if not UnitAffectingCombat("player") then return false end
    local n = 0
    for token in pairs(plates) do
        if UnitExists(token) and UnitCanAttack("player", token) and not UnitIsDead(token)
            and UnitAffectingCombat(token) then
            n = n + 1
            if n > 1 then return true end
        end
    end
    return false
end

local function Apply()
    if not UnitOutline then return end
    local db = UnitOutlineDB
    local on = db.enable
    if on == 1 and db.auto == 1 and not Engaged() then on = 0 end
    sent = on
    UnitOutline.Config(on, db.thickness, db.opacity,
        db.hostile[1], db.hostile[2], db.hostile[3],
        db.neutral[1], db.neutral[2], db.neutral[3],
        db.friendly[1], db.friendly[2], db.friendly[3],
        db.tsrc[1], db.tsrc[2], db.tsrc[3],
        db.msrc[1], db.msrc[2], db.msrc[3],
        db.players, db.npcs)
end

function UnitOutline_Toggle()
    local db = UnitOutlineDB
    db.enable = 1 - db.enable
    if enableCheck then enableCheck:SetChecked(db.enable == 1) end
    UIErrorsFrame:AddMessage("Unit Outline " .. (db.enable == 1 and "enabled" or "disabled"), 1, 1, 0, 1, UIERRORS_HOLD_TIME)
    Apply()
end

local function Check(name, label, y, key)
    local b = CreateFrame("CheckButton", name, panel, "UICheckButtonTemplate")
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, y)
    getglobal(name .. "Text"):SetText(label)
    b:SetChecked(UnitOutlineDB[key] == 1)
    b:SetScript("OnClick", function()
        if this:GetChecked() then UnitOutlineDB[key] = 1 else UnitOutlineDB[key] = 0 end
        Apply()
    end)
    return b
end

-- one cell of the source/reaction grid: UnitOutlineDB[key][i]
local function Cell(name, x, y, key, i)
    local b = CreateFrame("CheckButton", name, panel, "UICheckButtonTemplate")
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", x, y)
    b:SetChecked(UnitOutlineDB[key][i] == 1)
    b:SetScript("OnClick", function()
        if this:GetChecked() then UnitOutlineDB[key][i] = 1 else UnitOutlineDB[key][i] = 0 end
        Apply()
    end)
end

local function Slider(name, label, y, key, lo, hi, step)
    local s = CreateFrame("Slider", name, panel, "OptionsSliderTemplate")
    s:SetPoint("TOPLEFT", panel, "TOPLEFT", 20, y)
    s:SetWidth(200)
    s:SetMinMaxValues(lo, hi)
    s:SetValueStep(step)
    getglobal(name .. "Low"):SetText(lo)
    getglobal(name .. "High"):SetText(hi)
    local text = getglobal(name .. "Text")
    s:SetValue(UnitOutlineDB[key])
    text:SetText(label .. ": " .. UnitOutlineDB[key])
    s:SetScript("OnValueChanged", function()
        local v = floor(this:GetValue() / step + 0.5) * step
        UnitOutlineDB[key] = v
        text:SetText(label .. ": " .. v)
        Apply()
    end)
end

local function Swatch(name, label, x, y, key)
    local b = CreateFrame("Button", name, panel)
    b:SetWidth(20)
    b:SetHeight(20)
    b:SetPoint("TOPLEFT", panel, "TOPLEFT", x, y)
    b:SetNormalTexture("Interface\\ChatFrame\\ChatFrameColorSwatch")
    local tex = b:GetNormalTexture()
    local function Paint()
        local c = UnitOutlineDB[key]
        tex:SetVertexColor(c[1], c[2], c[3])
    end
    Paint()
    local text = b:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
    text:SetPoint("LEFT", b, "RIGHT", 6, 0)
    text:SetText(label)
    b:SetScript("OnClick", function()
        local r0, g0, b0 = UnitOutlineDB[key][1], UnitOutlineDB[key][2], UnitOutlineDB[key][3]
        ColorPickerFrame.func = function()
            local r, g, bl = ColorPickerFrame:GetColorRGB()
            UnitOutlineDB[key] = { r, g, bl }
            Paint()
            Apply()
        end
        ColorPickerFrame.cancelFunc = function()
            UnitOutlineDB[key] = { r0, g0, b0 }
            Paint()
            Apply()
        end
        ColorPickerFrame.hasOpacity = nil
        ColorPickerFrame.previousValues = { r0, g0, b0 }
        ColorPickerFrame:SetColorRGB(r0, g0, b0)
        ColorPickerFrame:Show()
    end)
end

local function Build()
    panel = CreateFrame("Frame", "UnitOutlineFrame")
    panel.name = "Unit Outline"
    panel.refresh = function() enableCheck:SetChecked(UnitOutlineDB.enable == 1) end

    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -16)
    title:SetText("Unit Outline")

    enableCheck = Check("UnitOutlineEnable", "Enable (keybind in Key Bindings)", -44, "enable")
    Check("UnitOutlineAuto", "Auto: only in combat with 2+ enemy nameplates in view", -70, "auto")
    local h = panel:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    h:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -108)
    h:SetText("Outline which units (click a swatch to recolor)")
    Swatch("UnitOutlineHostile", "Hostile", 110, -132, "hostile")
    Swatch("UnitOutlineNeutral", "Neutral", 190, -132, "neutral")
    Swatch("UnitOutlineFriendly", "Friendly", 270, -132, "friendly")
    local rows = { { "Target", "tsrc" }, { "Mouseover", "msrc" } }
    for r = 1, 2 do
        local y = -142 - r * 26
        local label = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
        label:SetPoint("TOPLEFT", panel, "TOPLEFT", 20, y - 6)
        label:SetText(rows[r][1])
        for i = 1, 3 do Cell("UnitOutline" .. rows[r][2] .. i, 106 + (i - 1) * 80, y, rows[r][2], i) end
    end
    Check("UnitOutlinePlayers", "Players", -230, "players")
    Check("UnitOutlineNpcs", "NPCs", -256, "npcs")
    Slider("UnitOutlineThickness", "Thickness", -305, "thickness", 1, 4, 0.5)
    Slider("UnitOutlineOpacity", "Opacity", -350, "opacity", 0.1, 1, 0.1)

    InterfaceOptions_AddCategory(panel)
end

local driver = CreateFrame("Frame")
driver:RegisterEvent("ADDON_LOADED")
driver:RegisterEvent("PLAYER_LOGIN")
driver:RegisterEvent("NAME_PLATE_UNIT_ADDED")
driver:RegisterEvent("NAME_PLATE_UNIT_REMOVED")
driver:SetScript("OnEvent", function()
    if event == "NAME_PLATE_UNIT_ADDED" then
        plates[arg1] = true
    elseif event == "NAME_PLATE_UNIT_REMOVED" then
        plates[arg1] = nil
    elseif event == "ADDON_LOADED" and arg1 == "UnitOutline" then
        if not UnitOutlineDB then UnitOutlineDB = {} end
        for k, v in pairs(defaults) do
            if UnitOutlineDB[k] == nil then UnitOutlineDB[k] = v end
        end
    elseif event == "PLAYER_LOGIN" then
        Build()
    end
end)

-- the DLL registers UnitOutline.Config on its first world render; after that, re-evaluate smart mode 4x/second
local elapsed = 0
driver:SetScript("OnUpdate", function()
    elapsed = elapsed + arg1
    if elapsed < 0.25 or not (UnitOutline and UnitOutlineDB) then return end
    elapsed = 0
    local db = UnitOutlineDB
    if sent == nil then
        Apply()
    elseif db.auto == 1 and db.enable == 1 then
        local on = Engaged() and 1 or 0
        if on ~= sent then Apply() end
    end
end)
