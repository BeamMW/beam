var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'wallet_status',
			params: {}
		}) + '\n');
});

client.on('data', function(data) {
	console.log('Received: ' + data);

	var res = JSON.parse(data);

	console.log("current_height:", res.result.current_height);
	console.log("current_state_hash:", res.result.current_state_hash);
	console.log("available:", res.result.available);
	console.log("receiving:", res.result.receiving);
	console.log("sending:", res.result.sending);
	console.log("maturing:", res.result.maturing);
	console.log("locked:", res.result.locked);

	client.destroy(); // kill client after server's response
});

client.on('close', function() {
	console.log('Connection closed');
});
