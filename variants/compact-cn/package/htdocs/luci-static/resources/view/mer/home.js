'use strict';
'require view';
'require rpc';
'require validation';

// SPDX-License-Identifier: MIT
var board = rpc.declare({ object: 'system', method: 'board' });
var info = rpc.declare({ object: 'system', method: 'info' });
var interfaces = rpc.declare({ object: 'network.interface', method: 'dump' });
var hostHints = rpc.declare({ object: 'luci-rpc', method: 'getHostHints' });
// procd localtime includes the router's timezone offset. Use the native UTC epoch.
var clock = rpc.declare({ object: 'luci', method: 'getUnixtime', expect: { result: 0 } });

function globalIPv6(address) {
	var a = typeof address === 'string' ? validation.parseIPv6(address) : null;
	// Conservative ordinary global-unicast candidate, never a reachability test.
	// Exclude protocol assignments/tunnels and documentation; ULA/link-local fail /3.
	return !!(a && (a[0] & 0xe000) === 0x2000 &&
		!(a[0] === 0x2001 && (a[1] < 0x200 || a[1] === 0xdb8)) &&
		a[0] !== 0x2002 && !(a[0] === 0x3fff && (a[1] & 0xf000) === 0));
}

function text(value) {
	return (value == null || value === '') ? '暂无数据' : String(value);
}
function bytes(value) {
	return typeof value === 'number' && Number.isFinite(value) && value >= 0
		? (value / 1048576).toFixed(1) + ' MiB' : '暂无数据';
}
function uptime(value) {
	if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) return '暂无数据';
	return Math.floor(value / 86400) + ' 天 ' + Math.floor(value % 86400 / 3600) + ' 小时 ' + Math.floor(value % 3600 / 60) + ' 分钟';
}
function row(label, value) {
	return E('div', { 'class': 'mer-row' }, [ E('dt', {}, label), E('dd', {}, [text(value)]) ]);
}
function link(title, description, path) {
	return E('a', { 'class': 'mer-shortcut', 'href': L.url.apply(L, path.split('/')) }, [
		E('strong', {}, title), E('span', {}, description), E('span', { 'class': 'mer-arrow', 'aria-hidden': 'true' }, '↗')
	]);
}

return view.extend({
	load: function() {
		return Promise.all([board(), info(), interfaces(), hostHints(), clock().then(function(ts) { return { unixtime: ts }; })].map(function(p) {
			return p.then(function(data) {
				return data && typeof data === 'object' && !Array.isArray(data) ? { ok: true, data: data } : { ok: false, data: {} };
			}, function() { return { ok: false, data: {} }; });
		}));
	},
	render: function(results) {
		var b = results[0].data, s = results[1].data, n = results[2].data;
		var mem = s.memory || {}, release = b.release || {};
		var lan = (Array.isArray(n.interface) ? n.interface : []).filter(function(i) { return i.interface === 'lan'; })[0];
		var all = Array.isArray(n.interface) ? n.interface : [];
		var wan = all.filter(function(i) { return i.interface === 'wan' || i.interface === 'wan6'; });
		var wanState = !results[2].ok ? '读取失败' : !wan.length ? '未配置 WAN' : wan.some(function(i) { return i.up; }) ? '接口已启动，联网状态未检测' : '接口未启动';
		var ipv6 = [];
		all.filter(function(i) { return i.up; }).forEach(function(i) {
			(i['ipv6-address'] || []).forEach(function(a) {
				if (globalIPv6(a.address) && ipv6.indexOf(a.address) < 0) ipv6.push(a.address);
			});
		});
		var hints = results[3].data || {}, hosts = Object.keys(hints).filter(function(k) { return hints[k] && typeof hints[k] === 'object'; });
		var addresses = [];
		if (lan) {
			(lan['ipv4-address'] || []).concat(lan['ipv6-address'] || []).forEach(function(a) {
				if (a.address) addresses.push(a.address + (a.mask == null ? '' : '/' + a.mask));
			});
		}
		var time = '暂无数据';
		var ts = results[4].data.unixtime;
		if (typeof ts === 'number' && Number.isFinite(ts) && ts >= 0 && ts < 1577836800) time = '未同步（早于 2020 年）';
		else if (typeof ts === 'number' && Number.isFinite(ts) && ts >= 1577836800) {
			try { time = new Intl.DateTimeFormat('zh-CN', { timeZone: 'Asia/Shanghai', year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23' }).format(new Date(ts * 1000)); }
			catch (e) { time = '浏览器不支持北京时间格式'; }
		}
		return E('div', { 'class': 'mer-home' }, [
			E('link', { 'rel': 'stylesheet', 'href': L.resource('mer/home.css') }),
			E('div', { 'class': 'mer-heading' }, [ E('p', { 'class': 'mer-eyebrow' }, '设备概览'), E('h2', {}, '设备状态与常用设置'), E('p', {}, '查看当前状态，进入 OpenWrt 原生页面完成详细设置。') ]),
			results.some(function(r) { return !r.ok; }) ? E('p', { 'class': 'alert-message warning', 'role': 'status' }, '部分状态读取失败，请稍后刷新或进入原生状态页查看。') : '',
			E('div', { 'class': 'mer-grid' }, [
				E('section', { 'class': 'mer-card' }, [ E('h3', {}, '系统'), E('dl', {}, [ row('设备型号', b.model), row('固件版本', release.description), row('内核版本', b.kernel), row('运行时间', uptime(s.uptime)) ]) ]),
				E('section', { 'class': 'mer-card' }, [ E('h3', {}, '网络连接'), E('dl', {}, [ row('LAN 状态', !results[2].ok ? '读取失败' : !lan ? '未找到 LAN 接口' : lan.up ? '已启动' : '未启动'), row('本机地址', addresses.length ? addresses.join(' · ') : '暂无地址'), row('WAN 状态', wanState), row('全球单播 IPv6', !results[2].ok ? '读取失败' : ipv6.length ? ipv6.join(' · ') : '未发现普通全球单播地址') ]), E('p', { 'class': 'mer-note' }, '不把 ULA 当公网地址。这里只识别地址类型，未测试互联网连通性或入站可达性。') ]),
				E('section', { 'class': 'mer-card' }, [ E('h3', {}, '内存与时间'), E('dl', {}, [ row('总内存', bytes(mem.total)), row('空闲内存', bytes(mem.free)), row('北京时间', time) ]), E('p', { 'class': 'mer-note' }, '仅按北京时间显示；不更改设备时区或校时设置。晚于 2020 年也不证明已经校准。') ]),
				E('section', { 'class': 'mer-card' }, [ E('h3', {}, '已识别设备'), E('dl', {}, [ row('设备记录', results[3].ok ? hosts.length + ' 条' : '读取失败') ]), E('p', { 'class': 'mer-note' }, '来自原生主机提示缓存，可能包含离线记录，不代表在线设备数量。完整信息请进入设备管理。') ])
			]),
			E('section', { 'class': 'mer-links' }, [ E('h3', {}, '常用设置'), E('div', { 'class': 'mer-shortcuts' }, [
				link('上网设置', '进入原生接口配置', 'admin/network/network'),
				link('设备管理', '查看原生网络与主机信息', 'admin/status/overview'),
				link('IPv6 规则', '查看与编辑原生通信规则', 'admin/network/firewall/rules'),
				link('系统设置', '时区、主机名等原生设置', 'admin/system/system'),
				link('高级 LuCI', '进入完整状态与高级菜单', 'admin/status/overview'),
				link('管理密码', '进入原生管理页面', 'admin/system/admin')
			]) ]),
			E('p', { 'class': 'mer-footnote' }, '此首页只读取状态，刷新页面可更新。完整菜单继续保留；网络和防火墙修改仍通过原生页面保存与应用。')
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
