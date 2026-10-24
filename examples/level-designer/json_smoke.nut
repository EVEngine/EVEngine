// Native JSON contract probes on the same VM used by the editor.
local samples=[];
for(local i=0;i<9409;++i) samples.push(i*0.001);
local encoded=eve.json.encode({schema="eve.json.probe",name="庭院",samples=samples,enabled=true,empty=null});
if(!encoded.ok) throw encoded.status.summary;
local decoded=eve.json.decode(encoded.value);
if(!decoded.ok) throw decoded.status.summary;
if(decoded.value.name!="庭院" || decoded.value.samples.len()!=9409 || !decoded.value.enabled || decoded.value.empty!=null)
    throw "Native JSON round trip changed the document";
local unicode=eve.json.decode("{\"name\":\"\\u5ead\\u9662\"}");
if(!unicode.ok || unicode.value.name!="庭院") throw "Native JSON Unicode escape failed";
local duplicate=eve.json.decode("{\"same\":1,\"same\":2}");
if(duplicate.ok) throw "Duplicate JSON key accepted";
local malformed=eve.json.decode("{broken");
if(malformed.ok) throw "Malformed JSON accepted";
local cycle={};cycle.self <- cycle;
local rejected=eve.json.encode(cycle);
if(rejected.ok) throw "Cyclic table accepted";
local unsupported=eve.json.encode({callback=function() {}});
if(unsupported.ok) throw "Unsupported JSON data accepted";
local after=eve.json.decode("[1,2,3]");
if(!after.ok || after.value.len()!=3) throw "JSON failure corrupted subsequent calls";
print("native-json smoke: PASS (9409 samples, UTF-8, Unicode escapes, malformed/duplicate/cyclic input)\n");
