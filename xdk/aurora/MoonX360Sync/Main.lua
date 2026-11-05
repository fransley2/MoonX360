-- Updates only the ContentItem at the shortcut's scan path and directory.
scriptTitle = "MoonX360 Shortcut Metadata"
scriptAuthor = "MoonX360"
scriptVersion = 4
scriptDescription = "Update each MoonX360 shortcut title and description"
scriptIcon = "icon.png"
scriptPermissions = { "sql", "filesystem", "content" }

local shortcutTitleId = tonumber("4D583353",16)
local readFile
local fallbackDescription = "PC game streamed through MoonX360."

-- Aurora builds do not promise bit32. FNV only xors one byte at a time.
local function xorByte(a,b)
    local out,place=0,1
    for _=1,8 do
        if a%2~=b%2 then out=out+place end
        a=math.floor(a/2);b=math.floor(b/2);place=place*2
    end
    return out
end

local function integer(data,at,big)
    local a,b,c,d=data:byte(at,at+3)
    if not d then return nil end
    if big then return ((a*256.0+b)*256.0+c)*256.0+d end
    return ((d*256.0+c)*256.0+b)*256.0+a
end
local function hash(data,n)
    local h=2166136261
    for i=1,n do
        local low=h%256
        local x=h-low+xorByte(low,data:byte(i))
        h=(x%65536*16777619 + (math.floor(x/65536)*16777619%65536)*65536)%4294967296
    end
    return h
end
local function field(data,first,last,required)
    local raw=data:sub(first,last)
    local zero=raw:find("\0",1,true)
    if not zero or (required and zero==1) or raw:sub(zero+1):find("[^\0]") then return nil end
    local value=raw:sub(1,zero-1)
    if value:find("[%c]") then return nil end
    return value
end
local function link(path)
    if not readFile or not FileSystem.FileExists(path) then return nil end
    local data=readFile(path)
    if not data or #data~=468 or data:sub(1,4)~="MX36" then return nil end
    local big=data:byte(5)==0 and data:byte(6)==1
    if not big and not (data:byte(5)==1 and data:byte(6)==0) then return nil end
    local size=big and data:byte(7)*256+data:byte(8) or data:byte(8)*256+data:byte(7)
    if size~=468 or hash(data,464)~=integer(data,465,big) then return nil end
    local host=field(data,9,88,true)
    local title=field(data,173,300,true)
    if not host or not title or not field(data,93,172,false) then return nil end
    return {title=title,host=data:sub(9,88),app=data:sub(89,92),big=big}
end
local function metadata(path,identity)
    local function parse(data)
        if not data or #data~=504 or data:sub(1,8)~="MX36META" or integer(data,9,identity.big)~=1 or
           data:sub(13,92)~=identity.host or data:sub(93,96)~=identity.app or
           hash(data,500)~=integer(data,501,identity.big) then return nil end
        local title=field(data,97,224,true)
        local description=field(data,225,480,false)
        local source=field(data,481,496,true)
        local id=integer(data,497,identity.big)
        if not title or not description or not id or
           not (source=="none" or source=="manual" or source=="sunshine") or
           (source=="manual" and id==0) or (source~="manual" and id~=0) then return nil end
        return {title=title,description=description,metadataTitleId=id}
    end
    if FileSystem.FileExists(path) then
        local found=parse(readFile(path));if found then return found end
    end
    if FileSystem.FileExists(path .. ".bak") then return parse(readFile(path .. ".bak")) end
    return nil
end
local function itemFolder(mount,directory)
    if type(mount)~="string" or type(directory)~="string" or
       directory:find("..",1,true) or directory:find("/",1,true) or directory:find(":",1,true) then return nil end
    local left,right=mount:sub(-1)=="\\",directory:sub(1,1)=="\\"
    if left and right then return mount .. directory:sub(2) end
    if not left and not right then return mount .. "\\" .. directory end
    return mount .. directory
end

function main()
    readFile = (FileSystem and FileSystem.ReadFile) or (Filesystem and Filesystem.ReadFile)
    if not readFile then
        Script.ShowMessageBox("MoonX360 shortcuts","This Aurora version cannot validate shortcut files.","OK")
        return
    end
    local scans,items,recognized,titles,descriptions,invalid=0,0,0,0,0,0
    for _,scan in pairs(Sql.ExecuteFetchRows("SELECT Id,DeviceId FROM ScanPaths")) do
        scans=scans+1
        local scanId,deviceId=tonumber(scan.Id),tonumber(scan.DeviceId)
        if scanId and deviceId then
            local mount=nil
            for _,device in pairs(Sql.ExecuteFetchRows("SELECT MountPoint FROM MountedDevices WHERE DeviceId == " .. deviceId)) do
                mount=device.MountPoint;break
            end
            if mount then
                local query="SELECT Id,Directory,Executable FROM ContentItems WHERE ScanPathId == " .. scanId .. " AND TitleId == " .. shortcutTitleId
                for _,item in pairs(Sql.ExecuteFetchRows(query)) do
                    items=items+1
                    local contentId=tonumber(item.Id)
                    if contentId and type(item.Executable)=="string" and item.Executable:lower()=="default.xex" then
                        local folder=itemFolder(mount,item.Directory)
                        if folder then
                        local identity=link(folder .. "\\moonx360.link")
                        if identity and FileSystem.FileExists(folder .. "\\default.xex") then
                            recognized=recognized+1
                            local extra=metadata(folder .. "\\moonx360.meta",identity)
                            if Content.SetTitle(contentId,extra and extra.title or identity.title) then titles=titles+1 end
                            local description=extra and extra.description or fallbackDescription
                            local info=Content.GetInfo(contentId)
                            local current=info and (info.Description or info.description) or ""
                            if description~=fallbackDescription or current=="" or current==fallbackDescription then
                                if Content.SetDescription(contentId,description) then descriptions=descriptions+1 end
                            end
                        end
                        if not identity then invalid=invalid+1 end
                        end
                    end
                end
            end
        end
    end
    if titles>0 then
        local refresh=Script.RefreshListOnExit or Script.SetRefreshListOnExit
        if refresh then refresh(true) end
    end
    Script.ShowMessageBox("MoonX360 shortcuts",string.format("Scan paths: %d\nTitle ID items: %d\nRecognized: %d\nTitles: %d\nDescriptions: %d\nMissing/invalid links: %d\nRestart Aurora if titles stay stale.",scans,items,recognized,titles,descriptions,invalid),"OK")
end
