const net = require('net');
const client = new net.Socket();

client.connect(10000, '127.0.0.1', function() {
	console.log('Connected')
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 'ev_subscribe',
			method: 'ev_subscribe',
			params: {}
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
client.on('data', function(data) {
	acc += data;

	// searching for \n symbol to find end of response
	if(data.indexOf('\n') !== -1)
	{
		let res = JSON.parse(acc)
		acc = ""
		console.log('Received:', res)
	}
})
