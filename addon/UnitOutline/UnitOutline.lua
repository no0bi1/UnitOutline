local defaults = {
    enable = 1, mouseover = 1, target = 1, thickness = 2, opacity = 1,
    hostile = { 1, 0.1, 0.1 }, neutral = { 1, 1, 0 }, friendly = { 0, 0.8, 0 },
}

local frame

local function Apply()
    local db = UnitOutlineDB
    UnitOutline.Config(db.enable, db.mouseover, db.target, db.thickness, db.opacity,
        db.hostile[1], db.hostile[2], db.hostile[3],
        db.neutral[1], db.neutral[2], db.neutral[3],
        db.friendly[1], db.friendly[2], db.friendly[3])
end

local function Check(name, label, y, key)
    local b = CreateFrame("CheckButton", name, frame, "UICheckButtonTemplate")
    b:SetPoint("TOPLEFT", frame, "TOPLEFT", 20, y)
    getglobal(name .. "Text"):SetText(label)
    b:SetChecked(UnitOutlineDB[key] == 1)
    b:SetScript("OnClick", function()
        if this:GetChecked() then UnitOutlineDB[key] = 1 else UnitOutlineDB[key] = 0 end
        Apply()
    end)
end

local function Slider(name, label, y, key, lo, hi, step)
    local s = CreateFrame("Slider", name, frame, "OptionsSliderTemplate")
    s:SetPoint("TOPLEFT", frame, "TOPLEFT", 25, y)
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

local function Swatch(name, label, y, key)
    local b = CreateFrame("Button", name, frame)
    b:SetWidth(20)
    b:SetHeight(20)
    b:SetPoint("TOPLEFT", frame, "TOPLEFT", 25, y)
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
        local c = UnitOutlineDB[key]
        ColorPickerFrame.func = function()
            local r, g, bl = ColorPickerFrame:GetColorRGB()
            UnitOutlineDB[key] = { r, g, bl }
            Paint()
            Apply()
        end
        ColorPickerFrame.cancelFunc = function(prev)
            UnitOutlineDB[key] = { prev[1], prev[2], prev[3] }
            Paint()
            Apply()
        end
        ColorPickerFrame.hasOpacity = nil
        ColorPickerFrame.previousValues = { c[1], c[2], c[3] }
        ColorPickerFrame:SetColorRGB(c[1], c[2], c[3])
        ShowUIPanel(ColorPickerFrame)
    end)
end

local function Build()
    frame = CreateFrame("Frame", "UnitOutlineFrame", UIParent)
    frame:SetWidth(260)
    frame:SetHeight(330)
    frame:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    frame:SetFrameStrata("DIALOG")
    frame:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })
    frame:SetMovable(true)
    frame:EnableMouse(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function() this:StartMoving() end)
    frame:SetScript("OnDragStop", function() this:StopMovingOrSizing() end)
    frame:Hide()
    tinsert(UISpecialFrames, "UnitOutlineFrame")

    local title = frame:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    title:SetPoint("TOP", frame, "TOP", 0, -16)
    title:SetText("Unit Outline")

    Check("UnitOutlineEnable", "Enable", -40, "enable")
    Check("UnitOutlineTarget", "Outline target", -66, "target")
    Check("UnitOutlineMouseover", "Outline mouseover", -92, "mouseover")
    Slider("UnitOutlineThickness", "Thickness", -135, "thickness", 1, 4, 0.5)
    Slider("UnitOutlineOpacity", "Opacity", -180, "opacity", 0.1, 1, 0.1)
    Swatch("UnitOutlineHostile", "Hostile", -215, "hostile")
    Swatch("UnitOutlineNeutral", "Neutral", -242, "neutral")
    Swatch("UnitOutlineFriendly", "Friendly", -269, "friendly")

    local close = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
    close:SetWidth(80)
    close:SetHeight(22)
    close:SetPoint("BOTTOM", frame, "BOTTOM", 0, 16)
    close:SetText("Close")
    close:SetScript("OnClick", function() frame:Hide() end)
end

local function AddMenuButton()
    local btn = CreateFrame("Button", "UnitOutlineMenuButton", GameMenuFrame, "GameMenuButtonTemplate")
    btn:SetText("Unit Outline")
    -- below the last custom button (UnitXP SP3) or Macros, whichever is there
    local above = UnitXP_SP3_GameMenuButton or GameMenuButtonMacros
    btn:SetPoint("TOP", above, "BOTTOM", 0, -1)
    local point, relativeTo, relativePoint, x, y = GameMenuButtonLogout:GetPoint()
    GameMenuButtonLogout:ClearAllPoints()
    GameMenuButtonLogout:SetPoint(point, btn, relativePoint, x, y)
    GameMenuFrame:SetHeight(GameMenuFrame:GetHeight() + btn:GetHeight() + 1)
    btn:SetScript("OnClick", function()
        PlaySound("igMainMenuOption")
        HideUIPanel(GameMenuFrame)
        frame:Show()
    end)
end

-- the DLL registers UnitOutline.Config on its first world render, so push the settings from OnUpdate
local pusher = CreateFrame("Frame")
pusher:RegisterEvent("ADDON_LOADED")
pusher:RegisterEvent("PLAYER_LOGIN")
pusher:SetScript("OnEvent", function()
    if event == "ADDON_LOADED" and arg1 == "UnitOutline" then
        if not UnitOutlineDB then UnitOutlineDB = {} end
        for k, v in pairs(defaults) do
            if UnitOutlineDB[k] == nil then UnitOutlineDB[k] = v end
        end
    elseif event == "PLAYER_LOGIN" then
        Build()
        AddMenuButton()
    end
end)
pusher:SetScript("OnUpdate", function()
    if UnitOutline and UnitOutlineDB then
        Apply()
        this:SetScript("OnUpdate", nil)
    end
end)
