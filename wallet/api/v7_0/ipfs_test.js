const net = require('net');
const client = new net.Socket();

client.setEncoding('utf8')
client.connect(10000, '127.0.0.1', function() {
    console.log('Connected')
    client.write(JSON.stringify(
        {
            jsonrpc: '2.0',
            id: 'ipfs_add',
            method: 'ipfs_add',
            params: {
                'data': [65, 66, 67, 68, 69, 10] // ABCDE\n
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
        acc = ""

        console.log('Received:', res)
        if (res.id === 'ipfs_add') {
            console.log("IPFS HASH:", res.result.hash)
            client.write(JSON.stringify(
                {
                    jsonrpc: '2.0',
                    id: 'ipfs_get',
                    method: 'ipfs_get',
                    params: {
                        'hash': res.result.hash
                    }
            }) + '\n')
        }

        onData(data.substring(br + 1))
    }
}

client.on('data', onData);
