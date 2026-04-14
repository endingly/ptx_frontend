from load_instuctions import load_instructions, Path
from load_instuctions import VariantModel


class VariantCodeGenerator:
    REQ_PTX_VERSION_FUNC_NAME = "require_ptx"
    REQ_SM_VERSION_FUNC_NAME = "require_sm"

    def __init__(self, model: VariantModel) -> None:
        self.model = model

    def gen_variant_check_function(self):
        """
        generate variant mode to cpp lambda function
        """

        # with function idx and function content
        shell_code = """
auto variant{} = [&]() {
{}
};
        """

        check_target_version_function = f"""
auto check_target_version = [&](){{
    bool flag = true;
    flag &= {self.REQ_PTX_VERSION_FUNC_NAME}({self.model.min_ptx_version},{self.model.description});
    flag &= {self.REQ_SM_VERSION_FUNC_NAME}({self.model.min_sm_version},{self.model.description});
}};
        """

    pass


if __name__ == "__main__":
    instructions = load_instructions(Path("instructions/test.yaml"))
    # for instr in instructions:
    #     print(f"Instruction: {instr.opcode}")
    #     for idx, variant in enumerate(instr.variants):
    #         print(f"  Variant {idx}: {variant.description}")

    print(
        f"cpp code check modifier: {instructions[0].variants[0].generate_code_for_type_check(0)}\n"
    )
