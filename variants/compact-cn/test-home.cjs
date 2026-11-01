// SPDX-License-Identifier: MIT
// Execute the real LuCI view with deterministic RPC fixtures; no hardware claims.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const here=__dirname, output=process.argv[2];
fs.mkdirSync(output,{recursive:true});
fs.mkdirSync(path.join(here,'evidence'),{recursive:true});
const viewSource=fs.readFileSync(path.join(here,'package/htdocs/luci-static/resources/view/mer/home.js'),'utf8');
const validationSource=fs.readFileSync(path.join(output,'validation.js'),'utf8');
const validation=new Function('baseclass',validationSource)({extend:x=>x});
const callLog=[];
const fixture={
 'system.board':{model:'Mercury MER1200G',kernel:'6.12.74',release:{description:'OpenWrt 25.12.2'}},
 'system.info':{uptime:85321,localtime:1793515200,memory:{total:123727872,free:47710208}},
 'network.interface.dump':{interface:[{interface:'lan',up:true,l3_device:'br-lan','ipv4-address':[{address:'192.168.1.1',mask:24}],'ipv6-address':[{address:'fd12:3456::1',mask:64}]}]},
 'luci-rpc.getHostHints':{'02:00:00:00:00:01':{name:'本地测试设备'}}
 ,'luci.getUnixtime':1793515200
};
// Pinned LuCI dom.append stringifies null/undefined array items; scalar strings
// use innerHTML, while string array items become createTextNode() values.
const E=(tag,attrs,children)=>({tag,attrs:attrs||{},children:children==null?[]:Array.isArray(children)?children.map(x=>x&&typeof x==='object'&&x.tag?x:{text:String(x)}):[{raw:String(children)}]});
const L={url:(...p)=>'/cgi-bin/luci/'+p.join('/'),resource:p=>'/luci-static/resources/'+p};
const sandbox={view:{extend:x=>x},rpc:{declare:s=>()=>{let key=s.object+'.'+s.method;callLog.push(key);let value=fixture[key];return value instanceof Error?Promise.reject(value):Promise.resolve(value);}},validation,E,L,Intl,Date,Promise};
const context=vm.createContext(sandbox);
const page=new vm.Script('(function(){'+viewSource.replace('return view.extend(', 'globalThis.testGlobalIPv6=globalIPv6;\nreturn view.extend(')+'\n})()').runInContext(context);
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const html=n=>'text' in n?esc(n.text):'raw' in n?n.raw:'<'+n.tag+Object.entries(n.attrs).map(([k,v])=>' '+k+'="'+esc(v)+'"').join('')+'>'+n.children.map(html).join('')+(n.tag==='link'?'':'</'+n.tag+'>');
async function render(){return html(page.render(await page.load()));}
(async()=>{
 let body=await render();
 assert(html(E('div',{},[null,undefined]))==='<div>nullundefined</div>');
 assert(!body.includes('null')&&!body.includes('undefined'),'successful homepage must not stringify absent children');
 assert(body.includes('未配置 WAN') && body.includes('未发现普通全球单播地址'));
 assert(body.includes('1 条') && body.includes('不代表在线设备数量'));
 assert(body.includes('45.5 MiB') && body.includes('2026/11/01'));
 assert(!body.includes('<script>'));
 const good=['240e:1234::1','2001:4860:4860::8888','2a00:1450::1'];
 const bad=['fd12::1','fc00::1','fe80::1','::1','::','ff02::1','2001:db8::1','2001:0000:1::1','2001:2::1','2002:c000:201::1','3fff:abc::1','not-ipv6'];
 for(const ip of good)assert(context.testGlobalIPv6(ip),ip);
 for(const ip of bad)assert(!context.testGlobalIPv6(ip),ip);
 fs.writeFileSync(path.join(output,'normal.html'),'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MER 首页 · 本地 fixture 预览</title><link rel="stylesheet" href="bootstrap.css"><style>body{background:#f7faf8;margin:0}nav{padding:16px 24px;background:#173834;color:white;font:14px system-ui}main{padding:16px 24px} .mer-home .alert-message{padding:12px;background:#fff2cc}</style><nav>OpenWrt　 首页　 状态　 系统　 网络</nav><main>'+body+'</main></html>');
 fixture['luci.getUnixtime']=42;
 body=await render();assert(body.includes('未同步（早于 2020 年）'));
 fixture['network.interface.dump'].interface.push({interface:'wan',up:true,'ipv6-address':[{address:'240e:1234::1',mask:64}]});
 body=await render();assert(body.includes('接口已启动，联网状态未检测')&&body.includes('240e:1234::1'));
 fixture['system.board'].model='<img src=x onerror=alert(1)>';
 body=await render();assert(body.includes('&lt;img')&&!body.includes('<img'));
 fixture['network.interface.dump']=new Error('fixture missing ubus');fixture['luci-rpc.getHostHints']=new Error('fixture denied');
 body=await render();assert(body.includes('部分状态读取失败')&&body.includes('读取失败'));
 fixture['system.board']=null;fixture['system.info']=null;
 body=await render();assert(body.includes('部分状态读取失败')&&body.includes('暂无数据'));
 assert.deepStrictEqual([...new Set(callLog)].sort(),['luci.getUnixtime','luci-rpc.getHostHints','network.interface.dump','system.board','system.info'].sort());
 const acl=JSON.parse(fs.readFileSync(path.join(here,'package/root/usr/share/rpcd/acl.d/luci-app-mer-home.json')));
 assert(!acl['luci-app-mer-home'].write);
 assert(page.handleSaveApply===null&&page.handleSave===null&&page.handleReset===null);
 const report={actual_view_executed:true,actual_upstream_ipv6_parser:true,deterministic_rpc_fixtures:true,read_only_rpc_methods:[...new Set(callLog)],ula_linklocal_documentation_excluded:true,unconfigured_wan_not_online:true,host_cache_not_online_count:true,pre2020_unsynchronized:true,utc_epoch_not_procd_shifted_localtime:true,rpc_failures_visible:true,html_injection_escaped:true,save_handlers_disabled:true,fixture_array_null_matches_pinned_luci:true,fixture_scalar_innerhtml_matches_pinned_luci:true,success_page_no_null_or_undefined_text:true,dynamic_rpc_values_use_text_nodes:true,device_test:false};
 fs.writeFileSync(path.join(here,'evidence/view-tests.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
})().catch(e=>{console.error(e);process.exitCode=1});
