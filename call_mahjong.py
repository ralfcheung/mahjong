import asyncio
import json
import sys
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

async def call_tool(tool_name, arguments):
    server_params = StdioServerParameters(
        command="/Users/ralfcheung/code/mahjong/mcp/run.sh",
        args=[],
        env=None
    )
    async with stdio_client(server_params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.call_tool(tool_name, arguments=arguments)
            for content in result.content:
                if content.type == "text":
                    return content.text
            return str(result)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python call_mahjong.py <tool_name> [args_json]")
        sys.exit(1)
    
    tool_name = sys.argv[1]
    args = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    
    try:
        output = asyncio.run(call_tool(tool_name, args))
        print(output)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
