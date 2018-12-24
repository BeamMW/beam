var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'get_utxo',
			params: {}
		}) + '\n');
});

var total = '';

client.on('data', function(data) {
	total += data;

	if(data.indexOf('\n') != -1)
	{
		var res = JSON.parse(total);

		console.log('Received:', res);

		client.destroy(); // kill client after server's response
	}
});

client.on('end', function() {
	console.log(">>>>>>>>>>>> END")
});

client.on('close', function() {
	console.log('Connection closed');
});
