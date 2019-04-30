var net = require('net');

var client = new net.Socket();
client.connect(10000, '127.0.0.1', function () {
    console.log('Connected');
    client.write(JSON.stringify(
		{
		    jsonrpc: '2.0',
		    id: 123,
		    method: 'start_swap',
		    params:
			{
			    "amount": 1000,
			    "fee": 10,
			    "swapAmount": 50000,
			    "beamSide": true,
			    "address": "10b741984e87cfee90b11133371a652c4e0421ecb5b8bc9be24834f90f161d94920"
			}
		}) + '\n');
});

client.on('data', function (data) {

    var res = JSON.parse(data);

    console.log("got:", res);

    client.destroy(); // kill client after server's response
});

client.on('close', function () {
    console.log('Connection closed');
});
