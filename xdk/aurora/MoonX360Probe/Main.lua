scriptTitle = "MoonX360 Script Probe"
scriptAuthor = "MoonX360"
scriptVersion = 1
scriptDescription = "Check which Aurora installation loads utility scripts"
scriptIcon = "icon.png"
scriptPermissions = {}

function main()
    Script.ShowMessageBox("MoonX360 Script Probe",
        "Aurora loaded this script from:\n" .. tostring(Script.GetBasePath()),"OK")
end
