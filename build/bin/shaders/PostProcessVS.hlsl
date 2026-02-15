// Post-Process Vertex Shader — Fullscreen triangle (no vertex buffer)
// Generates UVs for sampling the scene texture

struct PostVSOutput {
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
};

PostVSOutput VSMain(uint vertexID : SV_VertexID) {
    PostVSOutput output;

    // Generate fullscreen triangle from vertex ID (0, 1, 2)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.UV = uv;
    output.Position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.Position.y = -output.Position.y;  // Flip Y for DirectX

    return output;
}
