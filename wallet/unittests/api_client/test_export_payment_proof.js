var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'export_payment_proof',
			params:
			{
 				"txId" : "6e0cda184a3c48bea583e06d20040fca"
			}
		}) + '\n');
});

client.on('data', function(data) {
	console.log('Received: ' + data);

	var res = JSON.parse(data);

	console.log("payment_proof:", res.result.payment_proof);

	client.destroy(); // kill client after server's response
});

client.on('close', function() {
	console.log('Connection closed');
});
