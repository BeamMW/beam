var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'tx_send',
			params: 
			{
				"session" : 0,
				"value" : 12342342,
				"address" : "472e17b0419055ffee3b3813b98ae671579b0ac0dcd6f1a23b11a75ab148cc67"
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
