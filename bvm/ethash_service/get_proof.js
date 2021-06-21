var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function() {
	console.log('Connected');
	client.write(JSON.stringify(
		{
			jsonrpc: '2.0',
			id: 123,
			method: 'get_proof',
			params: {
				epoch: 416,
				seed: "fcbb65e35afc98de2ea3729c18d8fa3872e5088c82538e99e0cb58a5482cb17602a0c19b9dd0cb0b01f1db3769c9c0e48976240233855c5a11315aa9f2a1bb28"
			}
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
