from NovaAI.tools.nova import NovaBridge, Reply, capabilities

bridge = NovaBridge()
capabilities.install(bridge)     # 웹 / 다운로드 / Nova AI

bridge.serve_forever()