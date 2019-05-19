var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'delete_address',
			params: {address: '1a92710d38e2639c1a9988b44bfbf8c3116b075b395e207e3e96493e3100b641ea2'}
		}) + '\n');
});

var acc = '';

client.on('data', function(data) {
	acc += data;

	if(data.indexOf('\n') != -1)
	{
		var res = JSON.parse(acc);

		console.log('Received:', res);

		client.destroy(); // kill client after server's response
	}
});

client.on('close', function() {
	console.log('Connection closed');
});
