-- Host-only Aurora API simulation. Run with a Lua 5.3 compatible interpreter.
bit32={bxor=function(a,b)
    local out,bit=0,1
    for _=1,32 do
        if a%2~=b%2 then out=out+bit end
        a=math.floor(a/2);b=math.floor(b/2);bit=bit*2
    end
    return out
end}
local function pad(text,n) assert(#text<n);return text .. string.rep("\0",n-#text) end
local function u32(n)return string.char(math.floor(n/16777216)%256,math.floor(n/65536)%256,
    math.floor(n/256)%256,n%256) end
local function seal(payload) local h=2166136261
    for i=1,#payload do local x=bit32.bxor(h,payload:byte(i))
        h=(x%65536*16777619 + (math.floor(x/65536)*16777619%65536)*65536)%4294967296 end
    return payload .. u32(h)
end
local function link(host,app,title)
    return seal("MX36" .. string.pack(">I2I2",1,468) .. pad(host,80) ..
        u32(app) .. pad("",80) .. pad(title,128) ..
        pad("Hdd1:\\Apps\\MoonX360\\default.xex",160) .. u32(0))
end
local function meta(host,app,title,description,id)
    return seal("MX36META" .. string.pack(">I4",1) .. pad(host,80) ..
        u32(app) .. pad(title,128) .. pad(description,256) ..
        pad(id~=0 and "manual" or "none",16) .. u32(id))
end
local host="01234567-89ab-cdef-0123-456789abcdef"
local a,b="Hdd1:\\Shortcuts\\A","Hdd1:\\Shortcuts\\B"
local files={}
files[a .. "\\moonx360.link"]=link(host,10,"Hollow Knight")
files[a .. "\\moonx360.meta"]=meta(host,10,"Hollow Knight","A real game description.",0x545408A7)
files[a .. "\\default.xex"]="XEX2"
files[b .. "\\moonx360.link"]=link(host,11,"Blasphemous")
files[b .. "\\default.xex"]="XEX2"
FileSystem={FileExists=function(path)return files[path]~=nil end,
    ReadFile=function(path)return files[path] end}
Sql={ExecuteFetchRows=function(query)
    if query:find("FROM ScanPaths",1,true) then return {{Id=1,DeviceId=2}} end
    if query:find("FROM MountedDevices",1,true) then return {{MountPoint="Hdd1:"}} end
    assert(query:find("TitleId == " .. tonumber("4D583353",16),1,true))
    return {{Id=31,Directory="\\Shortcuts\\A",Executable="default.xex"},
            {Id=32,Directory="\\Shortcuts\\B",Executable="default.xex"}}
end}
local titles,descriptions={},{}
local realAuroraDescription=false
Content={SetTitle=function(id,title) titles[id]=title;return true end,
    SetDescription=function(id,description) descriptions[id]=description;return true end,
    GetInfo=function(id)return {Description=(realAuroraDescription and id==32) and "Existing Aurora description" or ""} end}
local refresh=false
Script={ShowMessageBox=function(_,value)print(value)end,
    RefreshListOnExit=function(value) refresh=value end}
bit32=nil -- Aurora's documented script API does not guarantee bit32.
local filesApi=FileSystem
FileSystem=nil -- Script discovery may load metadata before permissions are granted.
dofile(arg[1]);FileSystem=filesApi;main()
assert(titles[31]=="Hollow Knight" and titles[32]=="Blasphemous",tostring(titles[31]) .. " / " .. tostring(titles[32]))
assert(refresh)
assert(descriptions[31]=="A real game description.")
assert(descriptions[32]=="PC game streamed through MoonX360.")
files[a .. "\\moonx360.meta.bak"]=files[a .. "\\moonx360.meta"]
files[a .. "\\moonx360.meta"]="corrupt"
titles={};descriptions={};main()
assert(titles[31]=="Hollow Knight" and titles[32]=="Blasphemous")
assert(descriptions[31]=="A real game description.")
files[a .. "\\moonx360.meta.bak"]=nil
titles={};descriptions={};main()
assert(titles[31]=="Hollow Knight" and descriptions[31]=="PC game streamed through MoonX360.")
realAuroraDescription=true;titles={};descriptions={};main()
assert(titles[32]=="Blasphemous" and descriptions[32]==nil)
print("PASS: independent Aurora ContentItem titles, descriptions, v1 fallback, backup and corrupt sidecar")
