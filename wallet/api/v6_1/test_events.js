const net = require('net');
const client = new net.Socket();

client.setEncoding('utf8')
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected')
	/*client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 'ev_subscribe',
			method: 'addr_list',
			params: {
				//"asset_id": 1
			}
		}) + '\n')*/
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 'get_utxo',
			method: 'ev_subunsub',
			params: {
				'ev_utxos_changed': true
			}
		}) + '\n')
})

client.on('connect', function () {
	console.log ('Connected to the API...')
})

client.on('error', function (err) {
	console.log (err)
})

client.on('close', function() {
	console.log('Connection closed')
})

let acc = ''
function onData (data) {
	data = data.toString()
	let br = data.indexOf('\n')
	if (br === -1)
	{
		acc += data
	}
	else
	{
		acc += data.substring(0, br)

		let res = JSON.parse(acc);
		console.log('Received:', res)
		acc = ""

		onData(data.substring(br + 1))
	}
}

client.on('data', onData);
