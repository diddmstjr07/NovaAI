from tools.nova import capabilities, NovaBridge, Reply, VM

vm = VM()
bridge = NovaBridge()

if __name__ == "__main__":
    vm.on()
    capabilities.install(bridge)     # 웹 / 다운로드 / Nova AI
    bridge.serve_forever()