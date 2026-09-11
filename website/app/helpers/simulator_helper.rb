module SimulatorHelper
  SIMULATOR_ROOT = "simulator".freeze

  def simulator_build
    @simulator_build ||= Rails.public_path.join(SIMULATOR_ROOT).glob("*/embed.js").first&.dirname&.basename&.to_s
  end

  def simulator_module_path
    "/#{SIMULATOR_ROOT}/#{simulator_build}/embed.js" if simulator_build
  end
end
