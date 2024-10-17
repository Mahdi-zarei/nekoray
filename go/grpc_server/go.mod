module grpc_server

go 1.21

require (
	github.com/grpc-ecosystem/go-grpc-middleware v1.4.0
	github.com/matsuridayo/libneko v0.0.0-20230913024055-5277a5bfc889
	google.golang.org/grpc v1.64.1
	google.golang.org/protobuf v1.34.2
)

require (
	golang.org/x/net v0.28.0 // indirect
	golang.org/x/sys v0.24.0 // indirect
	golang.org/x/text v0.17.0 // indirect
	google.golang.org/genproto/googleapis/rpc v0.0.0-20240814211410-ddb44dafa142 // indirect
)

replace cloud.google.com/go => cloud.google.com/go v0.115.1
