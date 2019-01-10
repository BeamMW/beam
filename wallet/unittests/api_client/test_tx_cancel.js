var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'tx_cancel',
			params: 
			{
			    'txId': 'a13525181c0d45b0a4c5c1a697c8a7b8',
			}
		}) + '\n');
});

client.on('data', function(data) {

	var res = JSON.parse(data);

	console.log("got:", res);

	client.destroy(); // kill client after server's response
});

client.on('close', function() {
	console.log('Connection closed');
});
